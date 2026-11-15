import Foundation
import zlib

public enum ZipExtractorError: Error, LocalizedError {
    case invalidZipFile
    case fileNotFound
    case decompressionFailed(String)
    case writeFailed(String)
    
    public var errorDescription: String? {
        switch self {
        case .invalidZipFile:
            return "Geçersiz veya bozuk zip arşivi."
        case .fileNotFound:
            return "Arşiv dosyası bulunamadı."
        case .decompressionFailed(let msg):
            return "Ayıklama hatası: \(msg)"
        case .writeFailed(let msg):
            return "Dosya yazılamadı: \(msg)"
        }
    }
}

/// A single entry from the ZIP central directory.
public struct ZipEntryInfo {
    /// Normalized relative path ("/" separators, no leading/trailing slash).
    public let path: String
    public let isDirectory: Bool
    public let compressionMethod: UInt16
    public let compressedSize: UInt64
    public let uncompressedSize: UInt64
    public let localHeaderOffset: UInt64
    
    /// Path components without empty items.
    public var components: [String] {
        path.split(separator: "/").map(String.init)
    }
}

/// Streaming ZIP extractor.
/// - Reads the archive through a FileHandle (never loads the whole archive into RAM).
/// - Inflates entries chunk by chunk (safe for multi-GB Switch patch files).
/// - Supports ZIP64 archives (> 4 GB or > 65535 entries).
public final class ZipExtractor {
    
    private static let chunkSize = 1 << 20 // 1 MB
    
    // MARK: - Public API
    
    /// Lists all entries of the archive using the central directory.
    public static func listEntries(zipURL: URL) throws -> [ZipEntryInfo] {
        guard FileManager.default.fileExists(atPath: zipURL.path) else {
            throw ZipExtractorError.fileNotFound
        }
        let handle = try FileHandle(forReadingFrom: zipURL)
        defer { try? handle.close() }
        return try readCentralDirectory(handle: handle)
    }
    
    /// Convenience method that extracts all archive entries into `destinationURL`.
    public static func extract(
        zipURL: URL,
        to destinationURL: URL,
        progressHandler: ((Double) -> Void)? = nil
    ) throws {
        try extract(
            zipURL: zipURL,
            destinationFor: { entry in
                destinationURL.appendingPathComponent(entry.path)
            },
            progressHandler: progressHandler
        )
    }
    
    /// Extracts the given entries. `destinationFor` maps each entry to a target file URL,
    /// or returns nil to skip that entry. Progress is reported between 0.0 and 1.0.
    public static func extract(
        zipURL: URL,
        entries providedEntries: [ZipEntryInfo]? = nil,
        destinationFor: (ZipEntryInfo) -> URL?,
        progressHandler: ((Double) -> Void)? = nil
    ) throws {
        guard FileManager.default.fileExists(atPath: zipURL.path) else {
            throw ZipExtractorError.fileNotFound
        }
        let handle = try FileHandle(forReadingFrom: zipURL)
        defer { try? handle.close() }
        
        let entries = try providedEntries ?? readCentralDirectory(handle: handle)
        guard !entries.isEmpty else { throw ZipExtractorError.invalidZipFile }
        
        let fm = FileManager.default
        
        // Resolve targets first so progress reflects only the bytes that will actually be written
        var jobs: [(ZipEntryInfo, URL)] = []
        var totalBytes: UInt64 = 0
        for entry in entries {
            guard let target = destinationFor(entry) else { continue }
            jobs.append((entry, target))
            totalBytes += entry.isDirectory ? 0 : entry.uncompressedSize
        }
        
        var processedBytes: UInt64 = 0
        var lastReported: Double = -1
        let report: (UInt64) -> Void = { added in
            processedBytes += added
            guard let handler = progressHandler else { return }
            let value = totalBytes > 0 ? min(Double(processedBytes) / Double(totalBytes), 1.0) : 1.0
            if value - lastReported >= 0.01 || value >= 1.0 {
                lastReported = value
                handler(value)
            }
        }
        
        for (index, job) in jobs.enumerated() {
            let (entry, target) = job
            if entry.isDirectory {
                try fm.createDirectory(at: target, withIntermediateDirectories: true)
                continue
            }
            try fm.createDirectory(at: target.deletingLastPathComponent(), withIntermediateDirectories: true)
            try extractEntry(entry, from: handle, to: target, onBytesWritten: report)
            
            if totalBytes == 0, let handler = progressHandler {
                handler(Double(index + 1) / Double(jobs.count))
            }
        }
        progressHandler?(1.0)
    }
    
    // MARK: - Central Directory
    
    private static func readCentralDirectory(handle: FileHandle) throws -> [ZipEntryInfo] {
        let fileSize = try handle.seekToEnd()
        guard fileSize >= 22 else { throw ZipExtractorError.invalidZipFile }
        
        // Locate End Of Central Directory record (max comment length 65535)
        let tailLength = min(fileSize, UInt64(65_535 + 22))
        let tailStart = fileSize - tailLength
        let tail = try readBytes(handle, at: tailStart, count: Int(tailLength))
        
        var eocdIndex = -1
        if tail.count >= 22 {
            var i = tail.count - 22
            while i >= 0 {
                if tail.u32(at: i) == 0x06054b50 {
                    eocdIndex = i
                    break
                }
                i -= 1
            }
        }
        guard eocdIndex >= 0 else { throw ZipExtractorError.invalidZipFile }
        
        var totalEntries = UInt64(tail.u16(at: eocdIndex + 10))
        var cdSize = UInt64(tail.u32(at: eocdIndex + 12))
        var cdOffset = UInt64(tail.u32(at: eocdIndex + 16))
        
        // ZIP64 support
        if totalEntries == 0xFFFF || cdSize == 0xFFFF_FFFF || cdOffset == 0xFFFF_FFFF {
            let locatorIndex = eocdIndex - 20
            if locatorIndex >= 0, tail.u32(at: locatorIndex) == 0x07064b50 {
                let zip64EocdOffset = tail.u64(at: locatorIndex + 8)
                let zip64 = try readBytes(handle, at: zip64EocdOffset, count: 56)
                guard zip64.u32(at: 0) == 0x06064b50 else { throw ZipExtractorError.invalidZipFile }
                totalEntries = zip64.u64(at: 32)
                cdSize = zip64.u64(at: 40)
                cdOffset = zip64.u64(at: 48)
            }
        }
        
        guard cdOffset + cdSize <= fileSize else { throw ZipExtractorError.invalidZipFile }
        let cd = try readBytes(handle, at: cdOffset, count: Int(cdSize))
        
        var entries: [ZipEntryInfo] = []
        entries.reserveCapacity(Int(min(totalEntries, 200_000)))
        var p = 0
        
        while p + 46 <= cd.count, cd.u32(at: p) == 0x02014b50 {
            let flags = cd.u16(at: p + 8)
            let method = cd.u16(at: p + 10)
            var compSize = UInt64(cd.u32(at: p + 20))
            var uncompSize = UInt64(cd.u32(at: p + 24))
            let nameLen = Int(cd.u16(at: p + 28))
            let extraLen = Int(cd.u16(at: p + 30))
            let commentLen = Int(cd.u16(at: p + 32))
            var localOffset = UInt64(cd.u32(at: p + 42))
            
            let nameStart = p + 46
            let extraStart = nameStart + nameLen
            let next = extraStart + extraLen + commentLen
            guard next <= cd.count else { break }
            
            let nameData = cd.subdata(in: (cd.startIndex + nameStart)..<(cd.startIndex + extraStart))
            let rawName = decodeName(nameData, utf8Flag: (flags & 0x0800) != 0)
            
            // ZIP64 extended information extra field (0x0001)
            if compSize == 0xFFFF_FFFF || uncompSize == 0xFFFF_FFFF || localOffset == 0xFFFF_FFFF {
                var e = extraStart
                let extraEnd = extraStart + extraLen
                while e + 4 <= extraEnd {
                    let headerId = cd.u16(at: e)
                    let dataSize = Int(cd.u16(at: e + 2))
                    var q = e + 4
                    if headerId == 0x0001 {
                        if uncompSize == 0xFFFF_FFFF, q + 8 <= extraEnd { uncompSize = cd.u64(at: q); q += 8 }
                        if compSize == 0xFFFF_FFFF, q + 8 <= extraEnd { compSize = cd.u64(at: q); q += 8 }
                        if localOffset == 0xFFFF_FFFF, q + 8 <= extraEnd { localOffset = cd.u64(at: q); q += 8 }
                        break
                    }
                    e += 4 + dataSize
                }
            }
            
            let unified = rawName.replacingOccurrences(of: "\\", with: "/")
            let isDir = unified.hasSuffix("/")
            let normalized = unified
                .split(separator: "/")
                .map(String.init)
                .filter { !$0.isEmpty && $0 != "." }
                .joined(separator: "/")
            
            let comps = normalized.split(separator: "/")
            let unsafe = comps.contains("..")
            let junk = normalized.hasPrefix("__MACOSX") || normalized.hasSuffix(".DS_Store")
            
            if !normalized.isEmpty && !unsafe && !junk {
                entries.append(ZipEntryInfo(
                    path: normalized,
                    isDirectory: isDir,
                    compressionMethod: method,
                    compressedSize: compSize,
                    uncompressedSize: uncompSize,
                    localHeaderOffset: localOffset
                ))
            }
            
            p = next
        }
        
        return entries
    }
    
    private static func decodeName(_ data: Data, utf8Flag: Bool) -> String {
        if let s = String(data: data, encoding: .utf8) {
            return s
        }
        // Windows (Turkish OEM code page) fallback, then Latin-1
        let cp857 = String.Encoding(rawValue: CFStringConvertEncodingToNSStringEncoding(
            CFStringEncoding(CFStringEncodings.dosTurkish.rawValue)
        ))
        if let s = String(data: data, encoding: cp857) {
            return s
        }
        return String(data: data, encoding: .isoLatin1) ?? "unnamed"
    }
    
    // MARK: - Entry Extraction (streaming)
    
    private static func extractEntry(
        _ entry: ZipEntryInfo,
        from handle: FileHandle,
        to target: URL,
        onBytesWritten: (UInt64) -> Void
    ) throws {
        // Local file header
        let header = try readBytes(handle, at: entry.localHeaderOffset, count: 30)
        guard header.u32(at: 0) == 0x04034b50 else { throw ZipExtractorError.invalidZipFile }
        let nameLen = UInt64(header.u16(at: 26))
        let extraLen = UInt64(header.u16(at: 28))
        let dataStart = entry.localHeaderOffset + 30 + nameLen + extraLen
        try handle.seek(toOffset: dataStart)
        
        let fm = FileManager.default
        if fm.fileExists(atPath: target.path) {
            try fm.removeItem(at: target)
        }
        guard fm.createFile(atPath: target.path, contents: nil) else {
            throw ZipExtractorError.writeFailed(target.lastPathComponent)
        }
        let writer = try FileHandle(forWritingTo: target)
        defer { try? writer.close() }
        
        var written: UInt64 = 0
        
        switch entry.compressionMethod {
        case 0:
            // Stored
            var remaining = entry.compressedSize
            while remaining > 0 {
                let toRead = Int(min(remaining, UInt64(chunkSize)))
                guard let chunk = try handle.read(upToCount: toRead), !chunk.isEmpty else {
                    throw ZipExtractorError.invalidZipFile
                }
                try writer.write(contentsOf: chunk)
                remaining -= UInt64(chunk.count)
                written += UInt64(chunk.count)
                onBytesWritten(UInt64(chunk.count))
            }
            
        case 8:
            // Deflate (raw stream)
            written = try inflateEntry(entry, from: handle, writer: writer, onBytesWritten: onBytesWritten)
            
        default:
            throw ZipExtractorError.decompressionFailed("Desteklenmeyen sıkıştırma yöntemi (\(entry.compressionMethod)): \(entry.path)")
        }
        
        if entry.uncompressedSize > 0 && written != entry.uncompressedSize {
            throw ZipExtractorError.decompressionFailed("Eksik çıkartılan dosya: \(entry.path)")
        }
    }
    
    private static func inflateEntry(
        _ entry: ZipEntryInfo,
        from handle: FileHandle,
        writer: FileHandle,
        onBytesWritten: (UInt64) -> Void
    ) throws -> UInt64 {
        var stream = z_stream()
        let initStatus = inflateInit2_(&stream, -MAX_WBITS, ZLIB_VERSION, Int32(MemoryLayout<z_stream>.size))
        guard initStatus == Z_OK else {
            throw ZipExtractorError.decompressionFailed("zlib başlatılamadı")
        }
        defer { inflateEnd(&stream) }
        
        var outBuffer = [UInt8](repeating: 0, count: chunkSize)
        var remaining = entry.compressedSize
        var written: UInt64 = 0
        var streamEnded = false
        
        while remaining > 0 && !streamEnded {
            let toRead = Int(min(remaining, UInt64(chunkSize)))
            guard let chunk = try handle.read(upToCount: toRead), !chunk.isEmpty else {
                throw ZipExtractorError.invalidZipFile
            }
            remaining -= UInt64(chunk.count)
            
            try chunk.withUnsafeBytes { (inRaw: UnsafeRawBufferPointer) in
                guard let inBase = inRaw.bindMemory(to: Bytef.self).baseAddress else { return }
                stream.next_in = UnsafeMutablePointer<Bytef>(mutating: inBase)
                stream.avail_in = uInt(chunk.count)
                
                while true {
                    var status: Int32 = Z_OK
                    let produced: Int = outBuffer.withUnsafeMutableBytes { outRaw -> Int in
                        stream.next_out = outRaw.bindMemory(to: Bytef.self).baseAddress
                        stream.avail_out = uInt(chunkSize)
                        status = inflate(&stream, Z_NO_FLUSH)
                        return chunkSize - Int(stream.avail_out)
                    }
                    
                    if status == Z_STREAM_END {
                        streamEnded = true
                    } else if status != Z_OK && status != Z_BUF_ERROR {
                        throw ZipExtractorError.decompressionFailed("Deflate hatası (\(status)): \(entry.path)")
                    }
                    
                    if produced > 0 {
                        try writer.write(contentsOf: Data(outBuffer[0..<produced]))
                        written += UInt64(produced)
                        onBytesWritten(UInt64(produced))
                    }
                    
                    // Output buffer not full => current input chunk fully consumed (or stream ended)
                    if streamEnded || stream.avail_out != 0 {
                        break
                    }
                }
            }
        }
        
        if !streamEnded && entry.uncompressedSize > 0 && written < entry.uncompressedSize {
            throw ZipExtractorError.decompressionFailed("Deflate akışı eksik: \(entry.path)")
        }
        return written
    }
    
    // MARK: - Helpers
    
    private static func readBytes(_ handle: FileHandle, at offset: UInt64, count: Int) throws -> Data {
        if count <= 0 { return Data() }
        try handle.seek(toOffset: offset)
        guard let data = try handle.read(upToCount: count), data.count == count else {
            throw ZipExtractorError.invalidZipFile
        }
        return data
    }
}

private extension Data {
    func u16(at offset: Int) -> UInt16 {
        let i = startIndex + offset
        return UInt16(self[i]) | (UInt16(self[i + 1]) << 8)
    }
    
    func u32(at offset: Int) -> UInt32 {
        let i = startIndex + offset
        return UInt32(self[i])
            | (UInt32(self[i + 1]) << 8)
            | (UInt32(self[i + 2]) << 16)
            | (UInt32(self[i + 3]) << 24)
    }
    
    func u64(at offset: Int) -> UInt64 {
        UInt64(u32(at: offset)) | (UInt64(u32(at: offset + 4)) << 32)
    }
}
