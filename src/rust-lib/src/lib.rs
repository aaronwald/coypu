extern crate libc;
use libc::uint32_t;

#[no_mangle]
pub extern fn processRust (a: uint32_t) {
	println!("test from rust [{}]", a);
}

#[cfg(test)]
mod tests {
    #[test]
    fn it_works() {
        assert_eq!(2 + 2, 4);
    }
}
