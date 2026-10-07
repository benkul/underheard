#define PLUG_NAME "Drifter"
#define PLUG_MFR "Underheard"
#define PLUG_VERSION_HEX 0x00000100
#define PLUG_VERSION_STR "0.1.0"
#define PLUG_UNIQUE_ID 'Drft'
#define PLUG_MFR_ID 'Undh'
#define PLUG_URL_STR ""
#define PLUG_EMAIL_STR ""
#define PLUG_COPYRIGHT_STR "Copyright 2026 Underheard contributors"
#define PLUG_CLASS_NAME Drifter

#define BUNDLE_NAME "Drifter"
#define BUNDLE_MFR "Underheard"
#define BUNDLE_DOMAIN "com"

#define PLUG_CHANNEL_IO "0-2"
#define SHARED_RESOURCES_SUBPATH "Drifter"

#define PLUG_LATENCY 0
#define PLUG_TYPE 1
#define PLUG_DOES_MIDI_IN 1
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 1
#define PLUG_HAS_UI 1
#define PLUG_WIDTH 900
#define PLUG_HEIGHT 440 // the strip (with lanes) + the placeholder: see kStripTop in Drifter.cpp
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 0 // Drifter sizes itself around the synth's editor
#define PLUG_MIN_WIDTH 400
#define PLUG_MAX_WIDTH 6000
#define PLUG_MIN_HEIGHT 100
#define PLUG_MAX_HEIGHT 4000

#define AUV2_ENTRY Drifter_Entry
#define AUV2_ENTRY_STR "Drifter_Entry"
#define AUV2_FACTORY Drifter_Factory
#define AUV2_VIEW_CLASS Drifter_View
#define AUV2_VIEW_CLASS_STR "Drifter_View"

#define AAX_TYPE_IDS 'UDr1', 'UDr2'
#define AAX_PLUG_MFR_STR "Underheard"
#define AAX_PLUG_NAME_STR "Drifter\nDrft"
#define AAX_DOES_AUDIOSUITE 0
#define AAX_PLUG_CATEGORY_STR "Synth"

#define VST3_SUBCATEGORY "Instrument|Synth"
#define CLAP_MANUAL_URL "https://iplug2.github.io/manuals/example_manual.pdf"
#define CLAP_SUPPORT_URL "https://github.com/iPlug2/iPlug2/wiki"
#define CLAP_DESCRIPTION "iPlug2 instrument example"
#define CLAP_FEATURES "instrument"//, "synth"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64

#define ROBOTO_FN "Roboto-Regular.ttf"
