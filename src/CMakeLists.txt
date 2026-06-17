set(furble_sources
    FurbleControl.cpp
    FurbleGPS.cpp
    FurblePlatform.cpp
    FurbleSettings.cpp
    FurbleSpinValue.cpp
    FurbleUI.cpp
    FurbleUIFontZh14.c
    WaveshareEPaper154.cpp
    main.cpp)

idf_component_register(
    SRCS ${furble_sources}
    INCLUDE_DIRS "../include"
    REQUIRES driver esp_adc esp_timer esp_pm nvs_flash bt lvgl__lvgl h2zero__esp-nimble-cpp)
