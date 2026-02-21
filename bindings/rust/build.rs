use std::path::PathBuf;

fn main() {
    let mut build = cc::Build::new();

    build.file("../../lib/core.c")
         .file("../../lib/analyzer/analyzer.c")
         .file("../../lib/preprocess/bwt.c")
         .file("../../lib/preprocess/delta.c")
         .file("../../lib/preprocess/rle.c")
         .file("../../lib/model/context.c")
         .file("../../lib/entropy/huffman.c")
         .file("../../lib/entropy/ans.c")
         .file("../../lib/optimizer/genetic.c");

    build.include("../../include")
         .include("../../lib");

    // Enable OpenMP natively across all C compilers from Rust
    if cfg!(target_os = "windows") {
        build.flag("/openmp");
        println!("cargo:rustc-link-lib=vcomp");
    } else {
        build.flag("-fopenmp");
        println!("cargo:rustc-link-lib=gomp");
    }

    build.compile("maxcomp");
}
