extern crate libc;
use libc::uint32_t;

#[no_mangle]
pub extern fn processRust (a: uint32_t) {
	println!("test xx from rust [{}]", a);
    
    let v = vec![0, 2, 4, 6];
    println!("{}", v[1]); // it will display '2'
}

#[cfg(test)]
mod tests {
    #[test]
    fn it_works() {
        assert_eq!(2 + 2, 4);
    }
}
