idf_component_register(
    SRCS
        "src/hash_table.c"
        "src/node_globals.c"
        "src/node_table.c"
        "src/data_table.c"
        "src/lora_uart.c"
        "src/web_server.c"
        "main.c"

    INCLUDE_DIRS
        "include"

    EMBED_FILES 
        "templates/style.css"
        "templates/index.html"
)
