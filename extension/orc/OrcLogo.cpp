#include "OrcLogo.h"

#include <cmath>

namespace orc {
namespace {

// Everything in units of the cap height (1.0), origin bottom left.
constexpr double kStroke = 0.19;               // line weight
constexpr double kHalf   = kStroke / 2.0;
constexpr double kRing   = 0.5 - kHalf;        // centre-line radius of O and C
constexpr double kGap    = 0.17;               // space between letters
constexpr double kRWidth = 0.78;               // R, stem to leg
constexpr double kPi     = 3.14159265358979323846;

// Left edges of the three letters.
constexpr double kO = 0.0;
constexpr double kR = kO + 1.0 + kGap;
constexpr double kC = kR + kRWidth + kGap;
// C is open on the right; its width is the arc's reach at the opening angle.
constexpr double kCOpen = 40.0 * kPi / 180.0;

double cRight() { return kC + 0.5 + kRing * std::cos(kCOpen) + kHalf; }

}  // namespace

double wordmarkAspect() { return cRight(); }

void drawWordmark(CGContextRef ctx, CGRect box, CGColorRef color)
{
    const double h = box.size.height;
    const double w = wordmarkAspect() * h;
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, box.origin.x + (box.size.width - w) / 2.0, box.origin.y);
    CGContextScaleCTM(ctx, h, h);
    // Cap height and baseline are hard edges: the R's leg ends square on the
    // baseline instead of dipping under it with the corner of its cut.
    CGContextClipToRect(ctx, CGRectMake(-1.0, 0.0, wordmarkAspect() + 2.0, 1.0));
    CGContextSetStrokeColorWithColor(ctx, color);
    CGContextSetLineWidth(ctx, kStroke);
    CGContextSetLineJoin(ctx, kCGLineJoinRound);

    // O: a ring.
    CGContextSetLineCap(ctx, kCGLineCapButt);
    CGContextAddArc(ctx, kO + 0.5, 0.5, kRing, 0.0, 2.0 * kPi, 0);
    CGContextStrokePath(ctx);

    // R: stem, bowl, leg.
    const double stem   = kR + kHalf;
    const double top    = 1.0 - kHalf;
    const double waist  = 0.46;
    const double bowlR  = (top - waist) / 2.0;
    const double bowlX  = kR + kRWidth - kHalf - bowlR - 0.06;
    CGContextMoveToPoint(ctx, stem, 0.0);
    CGContextAddLineToPoint(ctx, stem, top);
    CGContextAddLineToPoint(ctx, bowlX, top);
    CGContextAddArc(ctx, bowlX, waist + bowlR, bowlR, kPi / 2.0, -kPi / 2.0, 1);
    CGContextAddLineToPoint(ctx, stem, waist);
    CGContextStrokePath(ctx);
    // The leg runs on past the baseline and the clip cuts it level there.
    const double legX0 = bowlX - 0.02, legX1 = kR + kRWidth - kHalf;
    const double over  = 0.2;
    CGContextMoveToPoint(ctx, legX0, waist);
    CGContextAddLineToPoint(ctx, legX1 + (legX1 - legX0) * over / waist, -over);
    CGContextStrokePath(ctx);

    // C: the ring, open to the right.
    CGContextAddArc(ctx, kC + 0.5, 0.5, kRing, kCOpen, 2.0 * kPi - kCOpen, 0);
    CGContextStrokePath(ctx);

    CGContextRestoreGState(ctx);
}

}  // namespace orc
