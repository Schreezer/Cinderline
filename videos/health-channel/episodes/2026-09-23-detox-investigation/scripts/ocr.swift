import Foundation
import Vision
import AppKit
for path in CommandLine.arguments.dropFirst() {
  guard let img = NSImage(contentsOfFile: path), let cg = img.cgImage(forProposedRect: nil, context: nil, hints: nil) else { print("FAIL", path); continue }
  let req = VNRecognizeTextRequest()
  req.recognitionLevel = .accurate
  req.usesLanguageCorrection = false
  try? VNImageRequestHandler(cgImage: cg, options: [:]).perform([req])
  print("== \((path as NSString).lastPathComponent) \(cg.width)x\(cg.height)")
  for o in (req.results ?? []) {
    let b = o.boundingBox
    let y = Int((1 - b.maxY) * Double(cg.height)), y2 = Int((1 - b.minY) * Double(cg.height))
    let x = Int(b.minX * Double(cg.width)), x2 = Int(b.maxX * Double(cg.width))
    print(String(format: "  [%4d-%4d y | %4d-%4d x] %@ (%.2f)", y, y2, x, x2, o.topCandidates(1).first?.string ?? "", o.confidence))
  }
}
