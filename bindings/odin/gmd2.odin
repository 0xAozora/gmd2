package gmd2

import encoder "encoder"
import decoder "decoder"
import types "types"

Bounds :: types.Bounds
Glyph :: types.Glyph
Artery_Advance :: types.Artery_Advance
Artery_Glyph :: types.Artery_Glyph
Y_Origin :: types.Y_Origin
Status :: types.Status
Atlas_View :: types.Atlas_View
Artery_Atlas_View :: types.Artery_Atlas_View
encode_bound :: encoder.encode_bound
encode :: encoder.encode
encode_artery :: encoder.encode_artery
encode_scratch_size :: encoder.encode_scratch_size
decode_size :: decoder.decode_size
decode :: decoder.decode
decode_artery :: decoder.decode_artery
decode_scratch_size :: decoder.decode_scratch_size
