#pragma once
// The suite's colours: violet, black and cream, plus one contrasting sage for the things placed
// in a room (sources, players) and red for recording. Every plugin draws from these, so a
// change here recolours them all.

#include "IGraphicsStructs.h"

namespace underheard_palette {

using iplug::igraphics::IColor;

// The three.
inline const IColor kViolet(255, 136, 74, 186);  // #884aba
inline const IColor kBlack(255, 0, 0, 0);        // #000000
inline const IColor kCream(255, 242, 232, 187);  // #f2e8bb

// Roles.
inline const IColor kBackground = kBlack;
inline const IColor kPanel(255, 20, 12, 27);         // views and rows: black lifted toward violet
inline const IColor kPanelRaised(255, 40, 24, 54);   // a selected panel
inline const IColor kControl(255, 46, 27, 62);       // knob and button bodies, bar tracks
inline const IColor kFrame(255, 96, 56, 128);        // control outlines
inline const IColor kText = kCream;
inline const IColor kTextDim(255, 170, 162, 131);    // values and hints: cream, dimmed
inline const IColor kAccent = kViolet;               // knob arcs, highlights, the mic
inline const IColor kContrast(255, 156, 204, 120);   // sources, players, playing (#9ccc78)
inline const IColor kRecord(255, 214, 66, 52);

// Rooms.
inline const IColor kFloor(255, 16, 10, 22);
inline const IColor kSoftWall(255, 74, 40, 102);
inline const IColor kHardWall = kCream;

// Splicer's tape.
inline const IColor kTape = kViolet;
inline const IColor kTapeWorn(255, 54, 30, 74);
inline const IColor kTapeEmpty(255, 30, 26, 34);
inline const IColor kSplice(255, 8, 4, 12);
inline const IColor kMark(90, 242, 232, 187);

} // namespace underheard_palette
