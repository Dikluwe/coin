struct Logger;
impl log::Log for Logger {
 fn enabled(&self, m:&log::Metadata)->bool {m.level()<=log::Level::Warn}
 fn log(&self,r:&log::Record){if self.enabled(r.metadata()){eprintln!("{} {} {}",r.level(),r.target(),r.args());}}
 fn flush(&self){}
}
static LOGGER:Logger=Logger;
fn main(){
 log::set_logger(&LOGGER).unwrap();log::set_max_level(log::LevelFilter::Warn);
 let i=wgpu::Instance::new(&wgpu::InstanceDescriptor::from_env_or_default());
 let a=pollster::block_on(i.request_adapter(&wgpu::RequestAdapterOptions{power_preference:wgpu::PowerPreference::HighPerformance,..Default::default()})).expect("adapter");
 println!("Adapter {:?}",a.get_info());println!("Limits {:?}",a.limits());println!("Downlevel {:?}",a.get_downlevel_capabilities());
 let result=pollster::block_on(a.request_device(&wgpu::DeviceDescriptor{label:Some("Coin diagnostic"),..Default::default()},None));
 match result {Ok((_d,_q))=>println!("device OK"),Err(e)=>println!("device error: {e}")};
}
