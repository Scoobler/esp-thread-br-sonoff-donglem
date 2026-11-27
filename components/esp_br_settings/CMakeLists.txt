set(srcs)
set(embed)
if(CONFIG_ESP_BR_SETTINGS)
    list(APPEND srcs "src/esp_br_settings.c")
    list(APPEND embed "web/settings.html" "web/radio.html")
endif()

set(priv_requires esp_event esp_http_server esp_ot_br_server esp_app_format esp_system nvs_flash openthread)
# For IDF < 6.0, use IDF's built-in json component; for IDF >= 6.0 use managed cJSON component
if("${IDF_VERSION_MAJOR}" VERSION_LESS "6")
    list(APPEND priv_requires json)
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES ${priv_requires}
                       EMBED_TXTFILES ${embed})
