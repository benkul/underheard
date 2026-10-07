#define PLUG_NAME "RoomBleed"
#define PLUG_MFR "Underheard"
#define PLUG_VERSION_HEX 0x00000100
#define PLUG_VERSION_STR "0.1.0"
#define PLUG_UNIQUE_ID 'RmBl'
#define PLUG_MFR_ID 'Undh'
#define PLUG_URL_STR ""
#define PLUG_EMAIL_STR ""
#define PLUG_COPYRIGHT_STR "Copyright 2026 Underheard contributors"
#define PLUG_CLASS_NAME RoomBleed

#define BUNDLE_NAME "RoomBleed"
#define BUNDLE_MFR "Underheard"
#define BUNDLE_DOMAIN "com"

#define SHARED_RESOURCES_SUBPATH "RoomBleed"

// Main input (mono or stereo), an optional stereo sidechain (for later), stereo out.
#define PLUG_CHANNEL_IO "\
1-1 \
1-2 \
1.2-1 \
1.2-2 \
2-2 \
2.2-2"

#define PLUG_LATENCY 0
#define PLUG_TYPE 0
#define PLUG_DOES_MIDI_IN 0
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 1
#define PLUG_HAS_UI 1
#define PLUG_WIDTH 1100
#define PLUG_HEIGHT 760
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0

#define AUV2_ENTRY RoomBleed_Entry
#define AUV2_ENTRY_STR "RoomBleed_Entry"
#define AUV2_FACTORY RoomBleed_Factory
#define AUV2_VIEW_CLASS RoomBleed_View
#define AUV2_VIEW_CLASS_STR "RoomBleed_View"

#define AAX_TYPE_IDS 'URb1', 'URb2'
#define AAX_TYPE_IDS_AUDIOSUITE 'URa1', 'URa2'
#define AAX_PLUG_MFR_STR "Underheard"
#define AAX_PLUG_NAME_STR "RoomBleed\nRmBl"
#define AAX_PLUG_CATEGORY_STR "Effect"
#define AAX_DOES_AUDIOSUITE 1

#define VST3_SUBCATEGORY "Fx"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define PLUG_HOST_RESIZE 0
#define APP_SIGNAL_VECTOR_SIZE 64

#define ROBOTO_FN "Roboto-Regular.ttf"
