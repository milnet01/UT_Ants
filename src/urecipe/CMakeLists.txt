# urecipe -- the recipe format, read and write, and finding a map's recipe.
# docs/specs/UTA-0113-recipe-format.md.
#
# A CONTENT VOCABULARY that crosses the seam (docs/design.md rule 3): the
# baker reads recipes now and the runtime will, so this links uta_core and
# nothing else -- no package reader, and no umat.

add_library(uta_urecipe STATIC
    Recipe.cpp
    Lookup.cpp)
target_link_libraries(uta_urecipe PUBLIC uta_core)

# SS 4.3's shipped recipes. There is no install step yet, so this is the
# repository's own recipes/ directory; packaging moves it beside the game.
target_compile_definitions(uta_urecipe PRIVATE
    UTA_SHIPPED_RECIPES_DIR="${PROJECT_SOURCE_DIR}/recipes")

# The seam, asserted at configure time in the form every sibling uses. A
# convenience link to a bake-side library would keep the runtime from reading
# recipes without anything failing until it tried.
get_target_property(_uta_urecipe_links uta_urecipe LINK_LIBRARIES)
if(NOT _uta_urecipe_links STREQUAL "uta_core")
    message(FATAL_ERROR
        "uta_urecipe links '${_uta_urecipe_links}'. docs/design.md rule 3 "
        "makes it a vocabulary the runtime reads, so it allows uta_core and "
        "nothing else.")
endif()
unset(_uta_urecipe_links)
