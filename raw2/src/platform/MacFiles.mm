// Fonctions de fichiers propres à macOS (Foundation). Compilé avec ARC.
#import <Foundation/Foundation.h>

#include <string>

namespace raw2::files {

// Met un fichier ou un dossier à la corbeille du Finder (récupérable, comme sous Windows).
bool macMoveToTrash(const std::string& path) {
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        if (!url) return false;
        NSError* err = nil;
        if ([[NSFileManager defaultManager] trashItemAtURL:url resultingItemURL:nil error:&err]) return true;
        // volume sans corbeille : suppression définitive, comme le ferait le Finder après confirmation
        return [[NSFileManager defaultManager] removeItemAtURL:url error:&err];
    }
}

} // namespace raw2::files
