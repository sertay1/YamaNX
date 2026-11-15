import SwiftUI
import UniformTypeIdentifiers

public struct FolderPicker: UIViewControllerRepresentable {
    public var onFolderSelected: (URL) -> Void
    
    public init(onFolderSelected: @escaping (URL) -> Void) {
        self.onFolderSelected = onFolderSelected
    }
    
    public func makeCoordinator() -> Coordinator {
        Coordinator(self)
    }
    
    public func makeUIViewController(context: Context) -> UIDocumentPickerViewController {
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: [.folder], asCopy: false)
        picker.allowsMultipleSelection = false
        picker.delegate = context.coordinator
        return picker
    }
    
    public func updateUIViewController(_ uiViewController: UIDocumentPickerViewController, context: Context) {}
    
    public final class Coordinator: NSObject, UIDocumentPickerDelegate {
        let parent: FolderPicker
        
        init(_ parent: FolderPicker) {
            self.parent = parent
        }
        
        public func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
            guard let url = urls.first else { return }
            parent.onFolderSelected(url)
        }
    }
}
