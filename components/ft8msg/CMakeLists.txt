# FT8 77-bit message packing from ft8_lib (Karlis Goba, MIT licence, see LICENSE).
idf_component_register(SRCS "ft8/message.c" "ft8/text.c" INCLUDE_DIRS ".")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format)
