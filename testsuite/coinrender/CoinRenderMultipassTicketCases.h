#ifndef COIN_RENDER_MULTIPASS_TICKET_CASES_H
#define COIN_RENDER_MULTIPASS_TICKET_CASES_H
bool ticketMetadata(const CoinRenderReadbackTicket &ticket,uint64_t serial) {
  // generation is an opaque resource-domain stamp, not a nonzero validity
  // flag. INITIAL BGFX assigns target.generation, whose initial value is0.
  const bool token=ticket.token!=0,submission=ticket.submissionSerial==serial;
  const bool extent=ticket.width==side&&ticket.height==side;
  const bool color=ticket.colorBytes==side*side*4&&ticket.colorRowPitch>=side*4;
  const bool depth=ticket.depthBytes==side*side*4&&ticket.depthRowPitch>=side*4&&ticket.depthFormat==1;
  std::cout<<"multipass_ticket_metadata token="<<ticket.token<<" generation="<<ticket.generation<<" serial="<<ticket.submissionSerial<<" expected_serial="<<serial<<" size="<<ticket.width<<'x'<<ticket.height<<" color_format="<<ticket.colorFormat<<" depth_format="<<ticket.depthFormat<<" color_pitch="<<ticket.colorRowPitch<<" depth_pitch="<<ticket.depthRowPitch<<" color_bytes="<<ticket.colorBytes<<" depth_bytes="<<ticket.depthBytes<<" valid_fields="<<token<<','<<submission<<','<<extent<<','<<color<<','<<depth<<'\n';
  return check(token&&submission&&extent&&color&&depth,"one complete immutable multipass color/depth ticket");
}
bool consumeTicket(const CoinRenderReadbackTicket &ticket,Image &image,const char *label) {
  SbString error;auto status=CoinRenderTarget::READBACK_NOT_READY;
  const auto start=std::chrono::steady_clock::now();
  do {
    status=CoinRenderTarget::pollReadback(ticket,image.rgba,image.depth,&error);
    if(status!=CoinRenderTarget::READBACK_NOT_READY)break;
  } while(std::chrono::steady_clock::now()-start<std::chrono::seconds(5));
  std::cout<<"multipass_ticket "<<label<<" status="<<status<<" error="<<error.getString()<<'\n';
  return check(status==CoinRenderTarget::READBACK_READY,std::string(label)+" completes its own final result without another apply or owner lifetime");
}
bool ticketPath(const CoinRenderOptions &options) {
  Scene scene;
  std::unique_ptr<CoinRenderTarget> target(CoinRenderTarget::createOffscreen(SbVec2i32(side,side),options));
  Audit *audit=prepare(target.get(),true);if(!audit)return false;
  std::unique_ptr<CoinRenderAction>action(new CoinRenderAction(SbViewportRegion(side,side)));
  action->setRenderTarget(target.get());action->setTransparencyType(CoinRenderAction::NONE);action->setBackgroundColor(SbColor4f(0,0,0,0));
  action->setNumPasses(2);action->apply(scene.root);
  check(action->getLastStatus()==CoinRenderAction::SUCCESS,"synchronous multipass predecessor before tickets");imageValid(read(target.get()),2,scene.edge,"ticket predecessor");
  CoinRenderReadbackTicket first{},second{};
  auto submit=[&](int n,CoinRenderReadbackTicket &ticket,const char *label) {
    action->setNumPasses(n);const auto serial=target->getLastSubmissionSerial(),source=scene.root->getNodeId();const unsigned captures=scene.captures;
    action->applyAsync(scene.root,ticket);
    const bool success=check(action->getLastStatus()==CoinRenderAction::SUCCESS&&target->getLastSubmissionSerial()>serial,std::string(label)+" admitted real multipass async: "+action->getLastError().getString());
    check(scene.captures==captures+1&&source==scene.root->getNodeId(),"async multipass has one immutable capture");
    if(success)ticketMetadata(ticket,target->getLastSubmissionSerial());
    const auto publication=read(target.get());
    check(publication.rgba.empty()&&publication.depth.empty(),"async hides synchronous accessors instead of exposing stale intermediate pixels");
  };
  submit(3,first,"first N3 ticket");
  const auto serial=target->getLastSubmissionSerial();const auto commands=audit->commands;const auto previous=read(target.get());
  action->setMultipassBufferBudget(0);CoinRenderReadbackTicket refused{};refused.token=987;
  action->applyAsync(scene.root,refused);
  check(action->getLastStatus()==CoinRenderAction::OUT_OF_MEMORY&&refused.token==0&&target->getLastSubmissionSerial()==serial&&audit->commands==commands&&exact(previous,read(target.get())),"budget refusal returns no new ticket, preserves prior async publication and submits nothing");
  action->setMultipassBufferBudget(256ull*1024*1024);scene.move(40.33);submit(7,second,"same-target N7 recovery ticket");
  check(first.token&&second.token&&first.token!=second.token,"overlapping multipass results own distinct tickets");
  std::vector<uint8_t> untouchedColor(1,17);std::vector<float> untouchedDepth(1,-1);
  auto malformed=first;malformed.colorRowPitch=0;
  check(CoinRenderTarget::pollReadback(malformed,untouchedColor,untouchedDepth)==CoinRenderTarget::READBACK_INVALID_TICKET&&untouchedColor==std::vector<uint8_t>(1,17)&&untouchedDepth==std::vector<float>(1,-1),"malformed metadata cannot consume the original pending ticket or change outputs");
  // Tickets own resources legitimately. No second target/runtime is retained
  // as a hidden lifetime anchor for the device or the accumulation scratch.
  action->setRenderTarget(nullptr);action.reset();
  check(target->resize(SbVec2i32(32,32)),"resize original owner while both tickets remain pending");target.reset();
  Image a,b;
  if(first.token&&consumeTicket(first,a,"N3 after owner destruction"))imageValid(a,3,32.33,"independent N3 ticket RGBA/LAST depth");
  if(second.token&&consumeTicket(second,b,"N7 after owner destruction"))imageValid(b,7,40.33,"independent N7 ticket RGBA/LAST depth");
  check(CoinRenderTarget::pollReadback(first,untouchedColor,untouchedDepth)==CoinRenderTarget::READBACK_INVALID_TICKET&&untouchedColor==std::vector<uint8_t>(1,17)&&untouchedDepth==std::vector<float>(1,-1),"completed multipass ticket is consumed exactly once");
  CoinRenderTarget::cancelReadback(first);CoinRenderTarget::cancelReadback(second);
  return good;
}
#endif
