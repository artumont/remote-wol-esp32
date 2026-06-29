idf_component_register(SRCS "main.c" "cloudflare.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES "cloudflare_root_ca.pem")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../.env")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../.env" CONFIG_LINES)
    foreach(LINE ${CONFIG_LINES})
        string(STRIP "${LINE}" LINE)
        
        if(NOT LINE MATCHES "^#" AND LINE MATCHES "=")
            string(REGEX REPLACE "^([^=]+)=(.*)$" "\\1;\\2" KEY_VALUE "${LINE}")
            list(GET KEY_VALUE 0 KEY)
            list(GET KEY_VALUE 1 VALUE)
            
            target_compile_definitions(${COMPONENT_LIB} PRIVATE ${KEY}="${VALUE}")
        endif()
    endforeach()
endif()
