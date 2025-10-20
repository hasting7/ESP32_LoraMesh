idf_component_register(
    SRCS
        "src/node_globals.c"
        "src/data_table.c"
        "src/lora_uart.c"
        "src/web_server.c"
        "main.c"

    INCLUDE_DIRS
        "include"

    EMBED_FILES 
        "style.css"
)
