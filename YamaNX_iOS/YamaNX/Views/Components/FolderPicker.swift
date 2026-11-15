import SwiftUI
import UIKit
import UniformTypeIdentifiers

public final class DocumentPickerHelper: NSObject, UIDocumentPickerDelegate {
    private static var activeInstance: DocumentPickerHelper?
    private let completion: (URL?) -> Void
    
    private init(completion: @escaping (URL?) -> Void) {
        self.completion = completion
        super.init()
    }
    
    public static func presentFolderPicker(initialDirectory: URL? = nil, completion: @escaping (URL?) -> Void) {
        DispatchQueue.main.async {
            guard let topVC = findTopViewController() else {
                completion(nil)
                return
            }
            
            let helper = DocumentPickerHelper(completion: completion)
            activeInstance = helper
            
            let picker = UIDocumentPickerViewController(forOpeningContentTypes: [.folder], asCopy: false)
            picker.allowsMultipleSelection = false
            picker.shouldShowFileExtensions = true
            picker.delegate = helper
            if let dir = initialDirectory {
                picker.directoryURL = dir
            }
            
            if let popover = picker.popoverPresentationController {
                popover.sourceView = topVC.view
                popover.sourceRect = CGRect(
                    x: topVC.view.bounds.midX,
                    y: topVC.view.bounds.midY,
                    width: 1,
                    height: 1
                )
                popover.permittedArrowDirections = []
            }
            
            topVC.present(picker, animated: true)
        }
    }
    
    public func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
        let pickedURL = urls.first
        DispatchQueue.main.async {
            self.completion(pickedURL)
            DocumentPickerHelper.activeInstance = nil
        }
    }
    
    public func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) {
        DispatchQueue.main.async {
            self.completion(nil)
            DocumentPickerHelper.activeInstance = nil
        }
    }
    
    private static func findTopViewController() -> UIViewController? {
        let scenes = UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }
            .filter { $0.activationState == .foregroundActive || $0.activationState == .foregroundInactive }
        
        let window = scenes.flatMap { $0.windows }.first(where: { $0.isKeyWindow })
            ?? scenes.flatMap { $0.windows }.first
        
        guard var topVC = window?.rootViewController else { return nil }
        while let presented = topVC.presentedViewController {
            topVC = presented
        }
        return topVC
    }
}
