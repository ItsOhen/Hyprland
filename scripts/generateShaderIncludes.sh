#!/bin/sh

SHADERS_SRC="./src/render/shaders/glsl"
OUTDIR="./src/render/shaders"

echo "-- Generating shader includes"

if [ ! -d "${OUTDIR}" ]; then
    mkdir -p "${OUTDIR}"
fi

HPP_TMP="$(mktemp)"
echo '#pragma once' > "${HPP_TMP}"
echo '#include <map>' >> "${HPP_TMP}"
echo 'static const std::map<std::string, std::string> SHADERS = {' >> "${HPP_TMP}"

for filename in $(ls "${SHADERS_SRC}"); do
    echo "--	${filename}"

    INC_TMP="$(mktemp)"
    { echo -n 'R"#('; cat "${SHADERS_SRC}/${filename}"; echo ')#"'; } > "${INC_TMP}"

    INC_DST="${OUTDIR}/${filename}.inc"
    if ! cmp -s "${INC_TMP}" "${INC_DST}" 2>/dev/null; then
        mv "${INC_TMP}" "${INC_DST}"
    else
        rm "${INC_TMP}"
    fi

    echo "{\"${filename}\"," >> "${HPP_TMP}"
    echo "#include \"./${filename}.inc\"" >> "${HPP_TMP}"
    echo "}," >> "${HPP_TMP}"
done

echo '};' >> "${HPP_TMP}"

HPP_DST="${OUTDIR}/Shaders.hpp"
if ! cmp -s "${HPP_TMP}" "${HPP_DST}" 2>/dev/null; then
    mv "${HPP_TMP}" "${HPP_DST}"
else
    rm "${HPP_TMP}"
fi
