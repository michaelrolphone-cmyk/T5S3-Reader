static size_t cases = 0, lazyCalls = 0;
static const EpdGlyph* lazyGlyph = nullptr;
static const EpdGlyph* lazy(void*, uint32_t) { ++lazyCalls; return lazyGlyph; }
static void compare(GfxRenderer& r, int id, const std::string& text, int width,
                    EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  for (bool role : {false, true}) {
    const auto expected = reference(r, id, text.c_str(), width, style, role);
    const auto actual = role ? truncatedPreparedText(r, id, text.c_str(), width, style)
                             : r.truncatedText(id, text.c_str(), width, style);
    if (actual != expected) {
      std::cerr << "id=" << id << " bytes=" << text.size() << " width=" << width << " role=" << role
                << " expected=" << expected << " actual=" << actual << '\n';
      assert(false);
    }
    ++cases;
  }
}
int main() {
  EpdFont ubuntu(&ubuntu_10_regular), noto(&notosans_12_regular);
  GfxRenderer r;
  r.fontMap.emplace(1, EpdFontFamily(&ubuntu, &noto, &ubuntu, &noto));
  for (size_t n : {128, 256, 512, 1024, 2048, 4096, 8192}) {
    const std::string text(n, 'A');
    compare(r, 1, text, 420);
    for (bool role : {false, true}) {
      r.calls = r.bytes = helperSteps = waits = 0;
      const auto result = role ? truncatedPreparedText(r, 1, text.c_str(), 420, EpdFontFamily::REGULAR)
                               : r.truncatedText(1, text.c_str(), 420);
      assert(result == std::string(26, 'A') + "\xe2\x80\xa6");
#ifdef ENFORCE_COST
      assert(r.calls == 1 && r.bytes == n && helperSteps == n);
      assert(waits == n / 256);
#endif
      if (role) std::cout << "bytes=" << n << " width_calls=" << r.calls << " measured_bytes=" << r.bytes
                          << " prefix_steps=" << helperSteps << " yields=" << waits << '\n';
    }
  }

  EpdGlyph glyphs[] = {{1,1,0,-20,1,0,0}, {1,1,16,30,1,0,0}, {7,1,8,-10,1,0,0}, {2,1,16,8,1,0,0}};
  EpdUnicodeInterval intervals[] = {{'A','C',0}, {0x2026,0x2026,3}};
  EpdKernClassEntry classes[] = {{'A',1}, {'B',2}, {'C',3}, {0x2026,4}};
  int8_t kern[16] = {-127,127,1,-120, 127,-127,3,120, 10,0,-120,1, -127,50,20,127};
  EpdFontData data{};
  data.glyph=glyphs; data.intervals=intervals; data.intervalCount=2;
  data.kernLeftClasses=data.kernRightClasses=classes;
  data.kernLeftEntryCount=data.kernRightEntryCount=4;
  data.kernLeftClassCount=data.kernRightClassCount=4; data.kernMatrix=kern;
  EpdFont unusual(&data); r.fontMap.emplace(2, EpdFontFamily(&unusual));
  bool nonmonotonic = false;
  std::mt19937 random(17);
  for (int sample=0; sample<3000; ++sample) {
    std::string text;
    for (int n=random()%80+1; n; --n) text += static_cast<char>('A'+random()%5);
    compare(r, 2, text, random()%500+1);
    int previous=0;
    for (size_t n=1;n<=text.size();++n) {
      int value=r.actualWidth(2,(text.substr(0,n)+"\xe2\x80\xa6").c_str(),EpdFontFamily::REGULAR);
      if (value<previous) nonmonotonic=true;
      previous=value;
    }
  }
  assert(nonmonotonic);

  const std::vector<std::string> words = {"", "A", "ABC", "fi", "fifififi", "AVAVAVAV", "a\xcc\x81" "bc",
      "\xcc\x81" "A", "\xe2\x80\xa6", "a\xc2\xad" "bc", "\xd7\x90\xd7\x91", "\xe4\xb8\xad\xe6\x96\x87",
      "x\xf0\x9f\x98\x80", std::string("bad\xff",4), std::string("\xe2\x82"), std::string("bad\0tail",8)};
  for (int id : {1,2,99}) for (const auto& word : words) for (int w : {-1,0,1,8,32,420})
    for (int style=0;style<8;++style) compare(r,id,word,w,static_cast<EpdFontFamily::Style>(style));
  // Initial fit uses <= while a truncated candidate must satisfy strict <.
  for (int n=1;n<40;++n) {
    std::string text(n,'A');
    int plain=r.actualWidth(1,text.c_str(),EpdFontFamily::REGULAR);
    int suffixed=r.actualWidth(1,(text+"\xe2\x80\xa6").c_str(),EpdFontFamily::REGULAR);
    for (int w : {plain-1,plain,plain+1,suffixed-1,suffixed,suffixed+1}) compare(r,1,text,w);
  }

  EpdLigaturePair pair{('A'<<16)|'B','C'};
  EpdFontData ligature=data; ligature.ligaturePairs=&pair; ligature.ligaturePairCount=1;
  EpdFont ligatureFont(&ligature); r.fontMap.emplace(3,EpdFontFamily(&ligatureFont));
  EpdFontData loading=data; loading.glyphMissHandler=lazy;
  EpdFont lazyFont(&loading); r.fontMap.emplace(4,EpdFontFamily(&lazyFont));
  r.fontMap.emplace(5,EpdFontFamily(&ubuntu)); r.sd.insert(5);
  EpdLigaturePair suffixPair{('A'<<16)|0x2026,'C'};
  EpdFontData suffixLigature=data; suffixLigature.ligaturePairs=&suffixPair; suffixLigature.ligaturePairCount=1;
  EpdFont suffixLigatureFont(&suffixLigature); r.fontMap.emplace(7,EpdFontFamily(&suffixLigatureFont));
  EpdLigaturePair missingPair{('D'<<16)|'A','C'};
  EpdFontData missingLigature=data; missingLigature.ligaturePairs=&missingPair; missingLigature.ligaturePairCount=1;
  EpdFont missingLigatureFont(&missingLigature); r.fontMap.emplace(8,EpdFontFamily(&missingLigatureFont));
  EpdFontData noSuffix=data; noSuffix.intervalCount=1;
  EpdFont noSuffixFont(&noSuffix); r.fontMap.emplace(6,EpdFontFamily(&noSuffixFont));
  for (int id : {3,4,5,6,7,8}) for (const auto& word : {std::string("ABABABAB"),std::string("DADBDCD"),std::string(128,'A')})
    for (int w : {1,10,20,40,120}) compare(r,id,word,w);
  // Refusal cannot invoke a lazy glyph callback or publish a partial prefix.
#ifndef BASELINE
  size_t prefix=12345;
  lazyCalls=0;
  for (int id : {4,5,99}) assert(!r.getTruncationPrefix(id,"DDDAAA",1,prefix) && prefix==12345);
  assert(!r.getTruncationPrefix(3,"AB",1,prefix) && prefix==12345);
  assert(!r.getTruncationPrefix(7,"AAA",1,prefix) && prefix==12345);
  assert(!r.getTruncationPrefix(8,"DA",1,prefix) && prefix==12345);
  assert(lazyCalls==0);
  for (const auto& word : {std::string(""),std::string(8193,'A'),std::string("A\xcc\x81"),std::string("A\xff"),std::string("A\0B",3)})
    assert(!r.getTruncationPrefix(1,word,1,prefix) && prefix==12345);
  // Normal retry after unsupported input; both item and elapsed-time checkpoints,
  // rollover, and the optional one-second deadline leave output unchanged.
  assert(r.getTruncationPrefix(1,std::string(512,'A'),420,prefix) && prefix==26);
  clockNow=UINT32_MAX-7;clockStep=0;waits=0;
  assert(r.getTruncationPrefix(1,std::string(8192,'A'),420,prefix) && prefix==26 && waits==32);
  clockNow=0;clockStep=4;waits=0;
  assert(r.getTruncationPrefix(1,std::string(100,'A'),420,prefix) && prefix==26 && waits>0);
  clockNow=0;clockStep=8;prefix=12345;waits=0;
  assert(!r.getTruncationPrefix(1,std::string(8192,'A'),420,prefix) && prefix==12345 && waits>0);
  clockStep=0;compare(r,1,std::string(1024,'A'),420);
#endif
  lazyGlyph=&glyphs[0];compare(r,4,"DDDAAADDD",25);lazyGlyph=nullptr;compare(r,4,"DDDAAADDD",25);
  compare(r,1,std::string(8193,'A'),420);
  assert(r.truncatedText(1,nullptr,400).empty());
  assert(truncatedPreparedText(r,1,nullptr,400,EpdFontFamily::REGULAR).empty());
  std::cout << "production truncation parity PASS: " << cases << " cases; real fonts, nonmonotonic bounds, fallback/retry/budgets\n";
}
