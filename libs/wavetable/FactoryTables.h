#pragma once
// Section's built-in wavetables. First those generated from spectra (no files): the basic
// shapes, and tables that morph across their frames - Bowed string (light to heavy bowing),
// Vowels (ah, eh, ee, oh, oo) and Organ (drawbar registrations, sparse to full); every frame of
// these is scaled to the same loudness (RMS 0.5). Then any tables registered from embedded
// files (the plugin registers horsi-vst3's horse tables), decoded like a loaded file.
//
// Building allocates and takes a moment: main thread only.

#include "WavetableData.h"

#include <cstddef>
#include <memory>

namespace underheard::wavetable
{

enum EFactoryTable { kSine = 0, kTriangle, kSaw, kSquare, kBowedString, kVowels, kOrgan, kNumGeneratedTables };

// A wavetable file built into the program (bytes of a .wav).
struct EmbeddedTable
{
  const char* name;
  const unsigned char* data;
  size_t size;
};
// Adds embedded tables after the generated ones (call once, at startup). The bytes must outlive
// every call below.
void RegisterEmbeddedTables(const EmbeddedTable* tables, int count);

int NumFactoryTables(); // generated + registered
const char* FactoryTableName(int index);
std::unique_ptr<WavetableData> MakeFactoryTable(int index);

} // namespace underheard::wavetable
