fn main() {
    let crate_dir = std::env::var_os("CARGO_MANIFEST_DIR").unwrap();
    match cbindgen::generate(crate_dir) {
        Ok(bindings) => bindings.write_to_file("target/webvtt-in-sei.h"),
        Err(cbindgen::Error::ParseSyntaxError { .. }) => return, // ignore in favor of cargo's syntax check
        Err(err) => panic!("{:?}", err),
    };
}
