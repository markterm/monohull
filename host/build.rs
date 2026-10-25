//! Builds Monohull's C DSP (../dsp) and the test shim with the host's C compiler (MSVC here).

fn main() {
    let dsp = "../dsp";
    let mut b = cc::Build::new();
    for f in ["mh_osc.c", "mh_resample.c", "mh_ladder.c", "mh_diode.c", "mh_svf.c", "mh_env.c", "mh_lfo.c", "mh_chorus.c",
              "mh_synth.c", "mh_params.c", "mh_tables.c", "mh_reverb.c", "mh_delay.c", "mh_ota.c", "mh_k35.c"] {
        b.file(format!("{dsp}/{f}"));
    }
    b.file("c/shim.c")
        .include(dsp)
        .opt_level(2)
        .flag_if_supported("/std:c11")
        .flag_if_supported("/fp:precise")
        .flag_if_supported("-std=c99")
        .define("MH_HOST_STATS", None)
        .warnings(true)
        .compile("monohull");
    // the references the ports are checked against (tests only), as their authors wrote them
    cc::Build::new()
        .cpp(true)
        .file("c/ref_mutable.cpp")
        .include("../ext/mutable")
        .define("TEST", None)
        .define("_USE_MATH_DEFINES", None)
        .opt_level(2)
        .flag_if_supported("/fp:precise")
        .warnings(false)
        .compile("monohull_ref");
    println!("cargo:rerun-if-changed=../ext/mutable");
    println!("cargo:rerun-if-changed={dsp}");
    println!("cargo:rerun-if-changed=c");
    println!("cargo:rerun-if-changed=../firmware/presets.h");
}
