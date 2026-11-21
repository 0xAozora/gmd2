fn main() {
    let mut build = cc::Build::new();
    let mut enabled = false;
    for (feature, source, header) in [
        (
            "CARGO_FEATURE_ENCODER",
            "../encoder.c",
            "../../include/gmd2_encoder.h",
        ),
        (
            "CARGO_FEATURE_DECODER",
            "../decoder.c",
            "../../include/gmd2_decoder.h",
        ),
    ] {
        if std::env::var_os(feature).is_some() {
            println!("cargo:rerun-if-changed={source}");
            println!("cargo:rerun-if-changed={header}");
            build.file(source);
            enabled = true;
        }
    }
    if enabled {
        build.std("c11").compile("gmd2");
    }
}
