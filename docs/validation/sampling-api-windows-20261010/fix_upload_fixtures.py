from pathlib import Path
root=Path(r'C:\Users\Diklu\.codex\worktrees\sampling-win-20261010\coin')
p=root/'testsuite/coinrender/CoinRenderAdvancedTextureTest.cpp';s=p.read_text(encoding='utf-8')
old='''  scene.texture->enableCompressedTexture = TRUE;
  scene.quality->textureQuality = .8f;
  action.apply(scene.root);'''
new='''  const auto firstUploadFilter = capture->plan.samplers[0].filter;
  scene.texture->enableCompressedTexture = TRUE;
  scene.quality->textureQuality = .8f;
  action.apply(scene.root);
  ok &= check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                  capture->plan.samplers[0].filter == firstUploadFilter,
              "quality hint without image notification retains first upload filter");
  // The current upload contract retains quality until image/wrap notification.
  // Reupload identical bytes before requiring the newly requested mip chain.
  scene.texture->image.touch();
  action.apply(scene.root);'''
assert old in s;s=s.replace(old,new,1)
old='''  scene.quality->textureQuality = .9f;
  action.apply(scene.root);'''
new='''  const auto priorUploadAnisotropy = capture->plan.samplers[0].maxAnisotropy;
  scene.quality->textureQuality = .9f;
  action.apply(scene.root);
  ok &= check(action.getLastStatus() == CoinRenderAction::SUCCESS &&
                  capture->plan.samplers[0].maxAnisotropy == priorUploadAnisotropy,
              "anisotropy hint without image notification retains first upload sampler");
  scene.texture->image.touch();
  action.apply(scene.root);'''
assert old in s;s=s.replace(old,new,1);p.write_text(s,encoding='utf-8',newline='\n')
p=root/'testsuite/coinrender/CoinRenderProceduralTextureTest.cpp';s=p.read_text(encoding='utf-8')
s=s.replace('    image->image.setValue(SbVec2s(16,16),3,pixels.data()); stages->addChild(image);','    image->image.setValue(SbVec2s(16,16),3,pixels.data()); stages->addChild(image); textures.push_back(image);',1)
old='''    scene.quality->textureQuality=.95f;
    if (!h.render(scene,"anisotropic-procedural-profile",true)) return false;'''
# Actual block uses two spaces, handled explicitly.
old=old.replace('    scene','  scene').replace('    if','  if')
new='''  const auto priorSampler = h.capture->frame.samplers[0];
  scene.quality->textureQuality=.95f;
  if (!h.render(scene,"quality-hint-retains-upload",true)) return false;
  if (!check(h.capture->frame.samplers[0].filter==priorSampler.filter &&
      h.capture->frame.samplers[0].maxAnisotropy==priorSampler.maxAnisotropy,
      "procedural quality hint preserves uploaded sampler before notification")) return false;
  for (auto* texture : scene.textures) texture->image.touch();
  if (!h.render(scene,"anisotropic-procedural-profile",true)) return false;'''
assert old in s;s=s.replace(old,new,1)
# Scene owns the nodes through its graph; this list only identifies upload notifications.
marker='  Scene(int shape = 0) {';assert marker in s;s=s.replace(marker,'  std::vector<SoTexture2*> textures;\n'+marker,1)
p.write_text(s,encoding='utf-8',newline='\n')
