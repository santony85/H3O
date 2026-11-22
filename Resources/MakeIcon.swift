// Dessine l'icône H3O (1024 × 1024) : swift Resources/MakeIcon.swift Resources/H3OIcon.png
//
// Une molécule H3O (un grand O, trois petits H) traversée par une onde sonore, sur un fond nuit,
// dans le carré arrondi des icônes macOS.
import AppKit
import CoreGraphics

let size: CGFloat = 1024
let output = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "H3OIcon.png"

let space = CGColorSpace (name: CGColorSpace.sRGB)!
let ctx = CGContext (data: nil, width: Int (size), height: Int (size), bitsPerComponent: 8, bytesPerRow: 0,
                     space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!

// Repère du haut à gauche, comme un dessin
ctx.translateBy (x: 0, y: size)
ctx.scaleBy (x: 1, y: -1)

func rgb (_ hex: UInt32, _ alpha: CGFloat = 1) -> CGColor {
    CGColor (srgbRed: CGFloat ((hex >> 16) & 0xff) / 255, green: CGFloat ((hex >> 8) & 0xff) / 255,
             blue: CGFloat (hex & 0xff) / 255, alpha: alpha)
}

func gradient (_ colors: [CGColor], _ locations: [CGFloat]) -> CGGradient {
    CGGradient (colorsSpace: space, colors: colors as CFArray, locations: locations)!
}

// Le carré arrondi (grille des icônes macOS : 824 px dans 1024)
let tile = CGRect (x: 100, y: 100, width: 824, height: 824)
let tilePath = CGPath (roundedRect: tile, cornerWidth: 185, cornerHeight: 185, transform: nil)

// Ombre portée douce
ctx.saveGState()
ctx.setShadow (offset: CGSize (width: 0, height: 14), blur: 30, color: rgb (0x000000, 0.45))
ctx.addPath (tilePath)
ctx.setFillColor (rgb (0x0b1220))
ctx.fillPath()
ctx.restoreGState()

// Fond : nuit bleue vers sarcelle profond
ctx.saveGState()
ctx.addPath (tilePath)
ctx.clip()
ctx.drawLinearGradient (gradient ([rgb (0x16203a), rgb (0x0a1a24), rgb (0x05262b)], [0, 0.55, 1]),
                        start: CGPoint (x: 512, y: 100), end: CGPoint (x: 512, y: 924), options: [])

// Halo derrière la molécule
let centre = CGPoint (x: 512, y: 470)
ctx.drawRadialGradient (gradient ([rgb (0x3fd0e8, 0.35), rgb (0x3fd0e8, 0)], [0, 1]),
                        startCenter: centre, startRadius: 0, endCenter: centre, endRadius: 360, options: [])

// Reflet en haut du carré
ctx.drawLinearGradient (gradient ([rgb (0xffffff, 0.10), rgb (0xffffff, 0)], [0, 1]),
                        start: CGPoint (x: 512, y: 100), end: CGPoint (x: 512, y: 420), options: [])
ctx.restoreGState()

// Liseré clair sur le bord
ctx.addPath (CGPath (roundedRect: tile.insetBy (dx: 2, dy: 2), cornerWidth: 183, cornerHeight: 183, transform: nil))
ctx.setStrokeColor (rgb (0xffffff, 0.12))
ctx.setLineWidth (4)
ctx.strokePath()

// Les trois H : en haut, en bas à gauche, en bas à droite (pyramide de l'ion H3O)
let ringRadius: CGFloat = 165
let atomRadius: CGFloat = 58
let atomDistance: CGFloat = 300
let angles: [CGFloat] = [-90, 150, 30].map { $0 * .pi / 180 }
let atoms = angles.map { CGPoint (x: centre.x + cos ($0) * atomDistance, y: centre.y + sin ($0) * atomDistance) }

// Liaisons
ctx.setLineCap (.round)
for (atom, angle) in zip (atoms, angles) {
    let from = CGPoint (x: centre.x + cos (angle) * (ringRadius + 34), y: centre.y + sin (angle) * (ringRadius + 34))
    let to = CGPoint (x: atom.x - cos (angle) * (atomRadius + 14), y: atom.y - sin (angle) * (atomRadius + 14))
    ctx.move (to: from)
    ctx.addLine (to: to)
}
ctx.setStrokeColor (rgb (0x7fe6f2, 0.55))
ctx.setLineWidth (16)
ctx.strokePath()

// Atomes H : sphères blanc → cyan
for atom in atoms {
    ctx.saveGState()
    ctx.addEllipse (in: CGRect (x: atom.x - atomRadius, y: atom.y - atomRadius, width: atomRadius * 2, height: atomRadius * 2))
    ctx.clip()
    let light = CGPoint (x: atom.x - atomRadius * 0.35, y: atom.y - atomRadius * 0.4)
    ctx.drawRadialGradient (gradient ([rgb (0xffffff), rgb (0x8fe9f5), rgb (0x2a9fc0)], [0, 0.45, 1]),
                            startCenter: light, startRadius: 0, endCenter: atom, endRadius: atomRadius * 1.1,
                            options: [.drawsAfterEndLocation])
    ctx.restoreGState()
}

// Le O : un anneau dégradé cyan → violet
ctx.saveGState()
ctx.addEllipse (in: CGRect (x: centre.x - ringRadius, y: centre.y - ringRadius, width: ringRadius * 2, height: ringRadius * 2))
ctx.setLineWidth (54)
ctx.replacePathWithStrokedPath()
ctx.clip()
ctx.drawLinearGradient (gradient ([rgb (0x4fe0f0), rgb (0x5aa8ff), rgb (0x8a7cff)], [0, 0.5, 1]),
                        start: CGPoint (x: centre.x - ringRadius, y: centre.y - ringRadius),
                        end: CGPoint (x: centre.x + ringRadius, y: centre.y + ringRadius), options: [])
ctx.restoreGState()

// L'onde sonore qui traverse l'anneau, plus ample au centre
let wave = CGMutablePath()
let span: CGFloat = 260
var first = true
for i in 0...240 {
    let t = CGFloat (i) / 240
    let x = centre.x - span + t * span * 2
    let envelope = sin (t * .pi)
    let y = centre.y - sin (t * .pi * 5) * 78 * envelope
    if first { wave.move (to: CGPoint (x: x, y: y)); first = false } else { wave.addLine (to: CGPoint (x: x, y: y)) }
}
ctx.saveGState()
ctx.setShadow (offset: .zero, blur: 18, color: rgb (0x3fd0e8, 0.9))
ctx.addPath (wave)
ctx.setStrokeColor (rgb (0xffffff))
ctx.setLineWidth (18)
ctx.setLineJoin (.round)
ctx.strokePath()
ctx.restoreGState()

// Le nom, en bas : H₃O
func drawText (_ text: String, font: NSFont, at point: CGPoint, color: NSColor) -> CGFloat {
    let attributed = NSAttributedString (string: text, attributes: [.font: font, .foregroundColor: color])
    let line = CTLineCreateWithAttributedString (attributed)
    let width = CTLineGetTypographicBounds (line, nil, nil, nil)
    ctx.saveGState()
    ctx.textMatrix = CGAffineTransform (scaleX: 1, y: -1)
    ctx.textPosition = point
    CTLineDraw (line, ctx)
    ctx.restoreGState()
    return CGFloat (width)
}

func textWidth (_ text: String, _ font: NSFont) -> CGFloat {
    CGFloat (CTLineGetTypographicBounds (CTLineCreateWithAttributedString (NSAttributedString (string: text, attributes: [.font: font])), nil, nil, nil))
}

let big = NSFont.systemFont (ofSize: 118, weight: .heavy)
let small = NSFont.systemFont (ofSize: 70, weight: .heavy)
let total = textWidth ("H", big) + textWidth ("3", small) + 6 + textWidth ("O", big)
var x = 512 - total / 2
let baseline: CGFloat = 852
let white = NSColor (white: 1, alpha: 0.95)
x += drawText ("H", font: big, at: CGPoint (x: x, y: baseline), color: white)
x += drawText ("3", font: small, at: CGPoint (x: x + 3, y: baseline + 22), color: NSColor (srgbRed: 0.55, green: 0.9, blue: 0.97, alpha: 1)) + 6
_ = drawText ("O", font: big, at: CGPoint (x: x, y: baseline), color: white)

// Enregistre en PNG
let image = ctx.makeImage()!
let rep = NSBitmapImageRep (cgImage: image)
try! rep.representation (using: .png, properties: [:])!.write (to: URL (fileURLWithPath: output))
print ("écrit \(output)")
