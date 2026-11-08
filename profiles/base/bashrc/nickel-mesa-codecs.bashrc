# NickelOS: hardware video decode for VP9 and AV1 on AMD GPUs.
#
# ChromiumOS builds its Mesa (media-libs/mesa-reven) with VA-API for H.264,
# HEVC and VC-1 only (-Dvideo-codecs="h264dec,h264enc,h265dec,h265enc,vc1dec"),
# so on AMD radeonsi offers no VP9 or AV1 decode and Chrome decodes YouTube's
# VP9 and AV1 on the CPU (found on the AMD test PC, a Ryzen laptop, 2026-10-06: the
# video engine stayed powered down while YouTube played). Intel decodes
# through its own media driver (iHD) and is not affected. Both codecs are
# royalty-free. The eclass appends MYMESONARGS after the ebuild's arguments,
# and meson takes the last value of an option.
MYMESONARGS="${MYMESONARGS} -Dvideo-codecs=h264dec,h264enc,h265dec,h265enc,vc1dec,vp9dec,av1dec"
