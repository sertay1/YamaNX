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

public final class ZipExtractor {
    
    public static func extract(zipURL: URL, to destinationURL: URL, progressHandler: ((Double) -> Void)? = nil) throws {
        guard let fileData = try? Data(contentsOf: zipURL) else {
            throw ZipExtractorError.fileNotFound
        }
        
        let entries = try parseCentralDirectory(data: fileData)
        guard !entries.isEmpty else {
            throw ZipExtractorError.invalidZipFile
        }
        
        let total = Double(entries.count)
        var count = 0.0
        
        for entry in entries {
            let relativePath = entry.fileName
            let targetURL = destinationURL.appendingPathComponent(relativePath)
            
            if entry.isDirectory {
                try FileManager.default.createDirectory(at: targetURL, withIntermediateDirectories: true)
            } else {
                let parentDir = targetURL.deletingLastPathComponent()
                try FileManager.default.createDirectory(at: parentDir, withIntermediateDirectories: true)
                
                let extractedData = try extractEntryData(data: fileData, entry: entry)
                try extractedData.write(to: targetURL)
            }
            
            count += 1.0
            progressHandler?(count / total)
        }
    }
    
    private struct ZipEntry {
        let fileName: String
        let compressionMethod: UInt16
        let compressedSize: UInt32
        let uncompressedSize: UInt32
        let localHeaderOffset: UInt32
        let isDirectory: Bool
    }
    
    private static func parseCentralDirectory(data: Data) throws -> [ZipEntry] {
        var entries = [ZipEntry]()
        let count = data.count
        guard count > 22 else { throw ZipExtractorError.invalidZipFile }
        
        // Find EOCD (End of Central Directory Record)
        var eocdOffset = -1
        let maxSearch = min(count, 65535 + 22)
        for i in stride(from: count - 22, through: count - maxSearch, by: -1) {
            if data[i] == 0x50 && data[i+1] == 0x4b && data[i+2] == 0x05 && data[i+3] == 0x06 {
                eocdOffset = i
                break
            }
        }
        guard eocdOffset != -1 else { throw ZipExtractorError.invalidZipFile }
        
        let cdOffset = Int(data.readUInt32(at: eocdOffset + 16))
        let totalEntries = Int(data.readUInt16(at: eocdOffset + 10))
        
        var currentOffset = cdOffset
        for _ in 0..<totalEntries {
            guard currentOffset + 46 <= count else { break }
            let sig = data.readUInt32(at: currentOffset)
            guard sig == 0x02014b50 else { break }
            
            let method = data.readUInt16(at: currentOffset + 10)
            let compSize = data.readUInt32(at: currentOffset + 20)
            let uncompSize = data.readUInt32(at: currentOffset + 24)
            let nameLen = Int(data.readUInt16(at: currentOffset + 28))
            let extraLen = Int(data.readUInt16(at: currentOffset + 30))
            let commentLen = Int(data.readUInt16(at: currentOffset + 32))
            let localHeaderOffset = data.readUInt32(at: currentOffset + 42)
            
            let nameData = data.subdata(in: (currentOffset + 46)..<(currentOffset + 46 + nameLen))
            let fileName = String(data: nameData, encoding: .utf8) ??
                           String(data: nameData, encoding: .isoLatin1) ?? "unnamed"
            
            let isDir = fileName.hasSuffix("/")
            entries.append(ZipEntry(
                fileName: fileName,
                compressionMethod: method,
                compressedSize: compSize,
                uncompressedSize: uncompSize,
                localHeaderOffset: localHeaderOffset,
                isDirectory: isDir
            ))
            
            currentOffset += 46 + nameLen + extraLen + commentLen
        }
        
        return entries
    }
    
    private static func extractEntryData(data: Data, entry: ZipEntry) throws -> Data {
        let offset = Int(entry.localHeaderOffset)
        guard offset + 30 <= data.count else { throw ZipExtractorError.invalidZipFile }
        let sig = data.readUInt32(at: offset)
        guard sig == 0x04034b50 else { throw ZipExtractorError.invalidZipFile }
        
        let nameLen = Int(data.readUInt16(at: offset + 26))
        let extraLen = Int(data.readUInt16(at: offset + 28))
        let dataStart = offset + 30 + nameLen + extraLen
        let dataEnd = dataStart + Int(entry.compressedSize)
        
        guard dataEnd <= data.count else { throw ZipExtractorError.invalidZipFile }
        let compressedData = data.subdata(in: dataStart..<dataEnd)
        
        if entry.compressionMethod == 0 {
            // Stored (no compression)
            return compressedData
        } else if entry.compressionMethod == 8 {
            // Deflate
            guard let decompressed = decompressRawDeflate(compressedData, expectedSize: Int(entry.uncompressedSize)) else {
                throw ZipExtractorError.decompressionFailed("Deflate açma başarısız: \(entry.fileName)")
            }
            return decompressed
        } else {
            throw ZipExtractorError.decompressionFailed("Desteklenmeyen sıkıştırma yöntemi: \(entry.compressionMethod)")
        }
    }
    
    private static func decompressRawDeflate(_ compressed: Data, expectedSize: Int) -> Data? {
        if expectedSize == 0 {
            return Data()
        }
        var stream = z_stream()
        // -MAX_WBITS instructs zlib to look for raw deflate stream (no zlib or gzip wrapper)
        var status = inflateInit2_(&stream, -MAX_WBITS, ZLIB_VERSION, Int32(MemoryLayout<z_stream>.size))
        guard status == Z_OK else { return nil }
        defer { inflateEnd(&stream) }
        
        var output = Data(count: expectedSize)
        let success: Bool = compressed.withUnsafeBytes { inBuffer in
            output.withUnsafeMutableBytes { outBuffer in
                stream.next_in = UnsafeMutablePointer<Bytef>(mutating: inBuffer.bindMemory(to: Bytef.self).baseAddress)
                stream.avail_in = uInt(compressed.count)
                stream.next_out = outBuffer.bindMemory(to: Bytef.self).baseAddress
                stream.avail_out = uInt(expectedSize)
                
                status = inflate(&stream, Z_FINISH)
                return status == Z_STREAM_END || status == Z_OK
            }
        }
        return success ? output : nil
    }
}

private extension Data {
    func readUInt16(at offset: Int) -> UInt16 {
        let sub = self.subdata(in: offset..<(offset + 2))
        return sub.withUnsafeBytes { $0.load(as: UInt16.self).littleEndian }
    }
    
    func readUInt32(at offset: Int) -> UInt32 {
        let sub = self.subdata(in: offset..<(offset + 4))
        return sub.withUnsafeBytes { $0.load(as: UInt32.self).littleEndian }
    }
}
