// gob2native - GOG installer import tests: the LZMA/LZMA2 decoders, SHA-1
// and MD5, path mapping, and (when the GOG installers are there) reading
// and identifying the three games' installers.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Usage: test_install [folder with the setup_*.exe files]
// (default: $GOB_SETUP_DIR; the installer tests are skipped without it).
// tests/check_install.sh compares every extracted file with innoextract's.

#include "install/gameinstall.h"
#include "install/zip.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace gob;
namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

namespace {

// Raw streams made with Python's lzma module from text() and random()
// below (scratch generator, not part of the build):
//   kLzma1:       LZMA1 lc=3 lp=0 pb=2, 64 KB dictionary, with end marker:
//                 text(3000, 1) + random(600, 7) + text(3000, 1)[:500]
//   kLzma1Lp:     LZMA1 lc=0 lp=2 pb=0: text(3000, 1)
//   kLzma2:       LZMA2: text(3000, 1) + its reverse
//   kLzma2Stored: LZMA2 of random(600, 7), which comes out as a stored chunk
const char *kLzma1 =
	"00309b88ab6c681b8116cc10850aa07e9c23d7235893dff3b5fa6ed58d64242a3536dcca6fd279b2f674be41713a6db8"
	"72f939ee2749c15741ad5ed429895c7edf467bd0e55b7afe1b7d10df63eaf46deda66eb68b640fef100fcea8b40dc64d"
	"f390af94994f37a509894a5942ae0b9c7d4b3d502c8de7ade320a2c5517fe3d4e10df792c08c9e9ec6edd580208ae02e"
	"7fc3bd71f2c05882b9eed40782e55444503d700c83a0ec1a0b74ef35efe87d1328683956d8be29966b4589e72c621533"
	"b171931d1f0b5daab7b16e4e127114c3628e87fafb89c7056773a9426501e837ca65cbbd47d8838729cdb574051853e2"
	"e8f76fcd2338ea6c060552892422b7b61de366aac200af7605d0a2365865e0634854c8ce6aabaaf825f1d46f0a358e7b"
	"3117af3c465c7d3512c44454f29df0225271afbb6052f5527d56f7891486a67a57053957a77a6bfec6a36c9ec94a248d"
	"d32847de376d4e4e726851a052b02c12b283b640d9c207f4ff2cb44381d62ce3a7180fad53c0b5925715cc53a179934e"
	"20e089a0b8a9a7f8a3f761270fbccbdc7cff90875f315466c7db01f8c0afcc7fd728f39a5cbe9f783671f09726a5d1b6"
	"414e5d01e3c1becfbf65b883f2555dec4e2642ce2ab2ba8d6e491d21a3636ea215d111351c62aab5556c18d1775820d4"
	"881029464a72249092190e9d3ddcc70e80f6a3b64e0f01fdaa14275c9ddc95ec241bd78f6e6151cf7b7ec08295a3bcfe"
	"ac9df164426d1362a5bbfec469f35b7bd894567b8d884142a693d7e8279bf634deb98017c5af237decec4d0b58e4fd38"
	"3934955956681ca48bc63277f1cd54526d274e2ff4fea7020ad270a757070f55a5bd96e67cf90444e3ea743813193bdc"
	"392ade059323c4f82404d7522bff7d764c1d1118bb5dd8d4b95cb0c209a3b90f77d0bd5eec17930f566d9e20d1575245"
	"81b9d7d163a981d64254ebd2519ed1d785cbbffe2f3053f2a6901fc970e4210896698f24e04be11bb4d07fcc9f3ccd31"
	"56e6c0e0d45354a1066f14399726e3877c55e6c74868e299544c6bd610b88cd49930a4ba8d17fb53e407dfec61e470a2"
	"9d9f4242804934209b8e8c5eb6530bb76b14b8842d3ee83459b8185a2ce4956d0fdc73a2040778b3bc21ffd0e2251a67"
	"5fe8b5310a70511ca08a2c0cea9c9d5adcd8fa420f4c1b29c8f0c1c93defcf473e17be5ca38a3810737f88a98f704027"
	"e7ba973243c3e6792c5df6d23321f5a58b89d1a6a7b72af077d4346058416156ac8af896477a1e3f5d79ef5711dbdc33"
	"d3b9532a3b51b2ecda23830487f6807dd761b850b9493b58b8792d451d5d1d49d1bb8b4b4f53c77369d6dd248a1913a7"
	"fbfb81a2fecdcd283df4747328e0e5b0028f2a15d19787daf17eea9485cbdba6a5c8caa30e1625cf804007e2cccb7704"
	"c54101bae39ca306e0ef16fdce8daca30a5d84d967c632c9978d8e47afacccc6f718504b698f4304e6764a1675004f2e"
	"8fed41c80fd144f635ae4d1b39bc4fb8cc721addd6f928e91f9205c75a8a6bd48adabae0b176532e0a946436a626aa0e"
	"1310ba0038cd612c11c2f8be806d82c3e8dec547b7556c978e7838a4b7f4673ef584634fd0e32fec472dbeea5e01754c"
	"22adaa61cad6b11557ddef0fb1b862e34cd2c77dc43d7829b49658b6d4b62c1682b5304ffaf8aabbafa9a3a0bb762f1d"
	"5b1e095f4d0ea770dd75253121c5155b97c7c3d20e36f49c3a0b4c98b0acfce09fff82cdc200"
	;
const char *kLzma1Lp =
	"00309c65b1729ede9e9bf9343dcb62c35db28616054fe718e6872e589f2ecc8fbf874e3fae9d16182c5496d2829a0fd7"
	"a2f033215210edd1f02034d996338328fb63e9eeb0c85fade5f062c69590834881312819f55faeffc010f420c264504c"
	"c4ce6aa835023e61b57288872f9956d5d8c8cc5098056b00fdb519a481b3fe9002f425ae4057aa6ed7552a227b67f049"
	"7b4e8b3d6d800f3f235e7a542965a315ac15f87408b4a7c1294517c2d1f1775da206bdf00337e384607264fc174dbadd"
	"7782f7222c61ea8d735efd7dbab3bfe539025866fe6f2fab75aac0adc36ded18aef69fa64b67d18630f87b93bae00783"
	"ad88c6d26bf2abe5e2310efc32ccd021c84dcda8c4424eb92d6c17395c5043b8e08a7a2178ec615d5e61f676cd49c813"
	"286db6a6f653b94eaee9d65e029bf8fd5052452d8eadfe17632c06a2354ebe1560597c9a1d19259d0592ad49d0f460bd"
	"1c45b7bc114b399d1da2bffb41276c97712e6d05bdb1ecbb52fd2f3fb7b405562e2de2c82c0ef824322957ea05f3cdbc"
	"4808b773f15bc44fa874c142b34b76a88df1aa0bf8a4996ddb9612f50c8265c874816ecdd2f790af9a9f127bf976b59c"
	"41e0856b0a6fe64c0adbbdcc7e7159b5dbbbf74325c0f6030209a6b5086b9ad5edbc1a17bead7afd53c5ec473ee1fec1"
	"3099064d2a32c3ff9161833dbe45d2494367cce26464606b968fd6be796a1ec8650c5e261dfa8c8479cf765bbdc8b721"
	"7ff2be76d12e7583c17f16dec982561410b234a4f7cb0fbb4fb30c36e2793470797f85485e32eda8d8aed276925f5441"
	"a2c27ffce757cbdcd7b773af840ef19b150e7a5174ace9ffffbb084408"
	;
const char *kLzma2 =
	"e0176f04895d00309b88ab6c681b8116cc10850aa07e9c23d7235893dff3b5fa6ed58d64242a3536dcca6fd279b2f674"
	"be41713a6db872f939ee2749c15741ad5ed429895c7edf467bd0e55b7afe1b7d10df63eaf46deda66eb68b640fef100f"
	"cea8b40dc64df390af94994f37a509894a5942ae0b9c7d4b3d502c8de7ade320a2c5517fe3d4e10df792c08c9e9ec6ed"
	"d580208ae02e7fc3bd71f2c05882b9eed40782e55444503d700c83a0ec1a0b74ef35efe87d1328683956d8be29966b45"
	"89e72c621533b171931d1f0b5daab7b16e4e127114c3628e87fafb89c7056773a9426501e837ca65cbbd47d8838729cd"
	"b574051853e2e8f76fcd2338ea6c060552892422b7b61de366aac200af7605d0a2365865e0634854c8ce6aabaaf825f1"
	"d46f0a358e7b3117af3c465c7d3512c44454f29df0225271afbb6052f5527d56f7891486a67a57053957a77a6bfec6a3"
	"6c9ec94a248dd32847de376d4e4e726851a052b02c12b283b640d9c207f4ff2cb44381d62ce3a7180fad53c0b5925715"
	"cc53a179934e20e089a0b8a9a7f8a3f761270fbccbdc7cff90875f315466c7db01f8c0afcc7fd728f39a5cbe9f783671"
	"f09726a5d1b6414e5d01e3c1becfbf65b883f2555dec4e2642ce2ab2ba8d6e491d21a3636ea215d111351c62aab5556c"
	"18d1775820d4881029464a72249092190e9d3ddcc70e80f6a3b64e0f01fdaa14275c9ddc95ec241bd78f6e6151cf7b7e"
	"c08295a3bcfeac9df164426d1362a5bbfec469f35b7bd894567b8d884142a693d7e8279bf634deb98017c5af237decec"
	"4d0b58e4fd383934955956681ca48bc63277f1cd54526d274e2ff4fea7020ad270a757070f4de5357288da6881d8534d"
	"9f63e5d1def480c412b1df7269f20c122cf0281772f970b2c533f3803944d0b31c8bc89751b18b46c99a321c3df1394b"
	"72094334b3335dd49becc9803d0bc76808e978949a3784d84e89de1bb55348f0872bf671dbfab38a7cb3aa8805be711e"
	"a62d693c510026cb821369a7b4aaf84ce8492aa90abb2b3d2ebdf4549c256f57a706c1195104275c892b549246c3ea6b"
	"966568c262a46ab5a5fd04e7949eafc0175e42de3eae938135bd35e28816670b6cbe5e703268e05880ed163956dae972"
	"3e99c2fa54e4b8d0389a7d113d1804ca4e8697a468b49827f5303ed31e1ec5b74202b9183702c9ba3a4d2fd005fae5f8"
	"65e3e2b375e05cf8fff09b2533daa7ba0ce67ab5299e5aba8006da4c30c54e2efd5ac27f830737f77fe806281a532778"
	"f6570ed90247f3ac036771bc552c4cd8247157853f12019f15f0dd8cb32ef56664ba34032f59f9f6eb75c95f642d4740"
	"9ab81eb6182727a4ed7870914ec584ca1e8801aea54807f81f071e21e4a5291fc2cec3c70d8bebf0860a9a474ef8b92d"
	"8b350acacc2d412aee5ab5d639e62e2da7311724d6d6613d7f840dab56e284f84ecc687e203df00342956b698a4b6b73"
	"b084158960aaa8636373ef31407b44fc22cb35a3b91fd44d5952a08554dd61181d746dceb016f315c5bd28c8d3d5da8d"
	"505c56f2b2669fa793423766248356d459d3947d5b0f7eabfcff4a26cdd13a1dbbec1a87aa2d587b95e9c345af6544a1"
	"e7925711ef50fcbba14acfb62d7dcc8b00"
	;
const char *kLzma2Stored =
	"0102576c4e74921325222e31a1cd13be12ed426966ce24fc23d7da8d2097616a06956ec28ad403136828d4571e3c5dee"
	"6e5ec04a91115f5d3b513ec253a416ad6ee5389411d0289aa34cf5c0347c59caf08495f3611b0b5068d59804f92eb729"
	"9955577999be78c01066870299e57f6cd235bcfb8f449deee23be1eccc8cbff5bfbec20bdbf86b9ce54f85b607cf46e9"
	"4a4b2afbd4e58f4fe15c11128218a32b18f772df8fd57a475bdee4743493265c919dd98be654598a9d0f1f0ed42adde0"
	"dcd85e906ead1cd9abea9ed3da8898dbe003c1427eeb72b84d2c03777b18e52f43397fb42ed9ca6a0b4dab6caf06137f"
	"6d55d9b9540152f12b8bb6e52d3c322e85f3cce488b0fb11b4df02d66c65105f716c198820ef724c6e042ff2a3ec3cf6"
	"d9dc3eb8348927e7de769babc9fd0695241f7a479a0b49e24d6f673495827c9f79ceccc7e9bec703c1ec6f817e276e37"
	"be45f38d7aaf50cb02a55544bc556a409ba06eaa61a7537e951776f14439bf5d77b97df37731fe1fc37df1bacebe7cf2"
	"792a1df9539a427092d1a692d18d712087500f104bebccf5cacf342d84132dcc4945d04c77f00cf1f0f1f9fddd7afd98"
	"26e3a17ead3431664d73153695aef2e844c7803a84022a18e75067ca2259dbdd8c4b2cd35465a58a85428d6cbbe6455c"
	"a38a245b342713feafc4e79057808106f05fa8a7fad4a178aa129369ad139e409c65b5493db73fba7f2772e834496a2c"
	"8cf70b9355db9c49f5bd20c3238d74ae68302a9a590b27669150fe6a710b0b6797eb502f1fd10f149c1a2b12d4ad3fbc"
	"3fc37be73e7943181c1786aec51edecf48136c130e0e71f3d801ad00"
	;

std::vector<uint8_t> unhex(const char *s) {
	std::vector<uint8_t> v;
	for (size_t i = 0; s[i] && s[i + 1]; i += 2) {
		unsigned b = 0;
		std::sscanf(s + i, "%2x", &b);
		v.push_back(uint8_t(b));
	}
	return v;
}

uint32_t lcg(uint32_t &seed) {
	seed = (seed * 1103515245u + 12345u) & 0x7FFFFFFF;
	return seed >> 16;
}

std::vector<uint8_t> text(size_t n, uint32_t seed) {
	static const char *words[] = {"goblin", "wizard", "potion", "prince", "buffoon", "castle", " ", "the ", "and ", "\n"};
	std::vector<uint8_t> out;
	while (out.size() < n) {
		const char *w = words[lcg(seed) % 10];
		out.insert(out.end(), w, w + std::strlen(w));
	}
	out.resize(n);
	return out;
}

std::vector<uint8_t> random(size_t n, uint32_t seed) {
	std::vector<uint8_t> out;
	for (size_t i = 0; i < n; i++)
		out.push_back(uint8_t(lcg(seed) & 255));
	return out;
}

struct MemorySource : ByteSource {
	std::vector<uint8_t> data;
	size_t pos = 0;
	size_t read(uint8_t *dst, size_t size) override {
		const size_t n = std::min(size, data.size() - pos);
		std::memcpy(dst, data.data() + pos, n);
		pos += n;
		return n;
	}
};

// Decodes `stream` in steps of `step` bytes (0: all at once).
std::vector<uint8_t> decode(const std::vector<uint8_t> &stream, bool lzma2, uint8_t props, size_t step,
                            bool *finished = nullptr, bool *failed = nullptr) {
	MemorySource src;
	src.data = stream;
	LzmaDecoder d;
	std::vector<uint8_t> out;
	bool ok = lzma2 ? d.initLzma2(&src, props) : d.initLzma1(&src, props, 1 << 16);
	if (ok) {
		for (;;) {
			const size_t n = step ? step : 1 << 20;
			std::vector<uint8_t> buf(n);
			const size_t got = d.decode(buf.data(), n);
			out.insert(out.end(), buf.begin(), buf.begin() + got);
			if (got < n)
				break;
		}
	}
	if (finished)
		*finished = d.finished();
	if (failed)
		*failed = d.failed() || !ok;
	return out;
}

std::vector<uint8_t> concat(std::vector<uint8_t> a, const std::vector<uint8_t> &b) {
	a.insert(a.end(), b.begin(), b.end());
	return a;
}

void testHashes() {
	Sha1 sha;
	sha.update(reinterpret_cast<const uint8_t *>("abc"), 3);
	CHECK(hexDigest(sha.finish()) == "a9993e364706816aba3e25717850c26c9cd0d89d");
	Md5 md5;
	md5.update(reinterpret_cast<const uint8_t *>("abc"), 3);
	CHECK(hexDigest(md5.finish()) == "900150983cd24fb0d6963f7d28e17f72");
	CHECK(hexDigest(md5.finish()) == "d41d8cd98f00b204e9800998ecf8427e"); // empty, after the reset
	// In pieces of odd sizes, across the 64-byte blocks.
	const std::vector<uint8_t> t = text(3000, 1);
	for (size_t piece : {1, 7, 63, 64, 65, 1000}) {
		for (size_t i = 0; i < t.size(); i += piece) {
			sha.update(t.data() + i, std::min(piece, t.size() - i));
			md5.update(t.data() + i, std::min(piece, t.size() - i));
		}
		CHECK(hexDigest(sha.finish()) == "ce34dc950fdf7cc69d8ad1729b1852d3e0098023");
		CHECK(hexDigest(md5.finish()) == "fc26bfab178106f9d700dd2645591af4");
	}
}

void testLzma() {
	const std::vector<uint8_t> t = text(3000, 1), r = random(600, 7);
	const std::vector<uint8_t> plain1 = concat(concat(t, r), std::vector<uint8_t>(t.begin(), t.begin() + 500));
	const uint8_t props1 = (2 * 5 + 0) * 9 + 3, propsLp = (0 * 5 + 2) * 9 + 0;
	const uint8_t dict2 = 10; // 64 KB: (2 | 0) << (10 / 2 + 11)
	for (size_t step : {0, 1, 3, 100, 4099, 4100, 4101}) {
		bool finished = false, failed = true;
		CHECK(decode(unhex(kLzma1), false, props1, step, &finished, &failed) == plain1);
		CHECK(finished && !failed);
		CHECK(decode(unhex(kLzma1Lp), false, propsLp, step, &finished, &failed) == t);
		CHECK(finished && !failed);
		std::vector<uint8_t> rev(t.rbegin(), t.rend());
		CHECK(decode(unhex(kLzma2), true, dict2, step, &finished, &failed) == concat(t, rev));
		CHECK(finished && !failed);
		CHECK(decode(unhex(kLzma2Stored), true, dict2, step, &finished, &failed) == r);
		CHECK(finished && !failed);
	}
	// Truncated and damaged streams fail without crashing.
	std::vector<uint8_t> cut = unhex(kLzma1);
	cut.resize(cut.size() / 2);
	bool finished = true, failed = false;
	std::vector<uint8_t> out = decode(cut, false, props1, 0, &finished, &failed);
	CHECK(failed && !finished && out.size() < plain1.size());
	std::vector<uint8_t> cut2 = unhex(kLzma2);
	cut2.resize(cut2.size() - 10);
	decode(cut2, true, dict2, 0, &finished, &failed);
	CHECK(failed && !finished);
	for (size_t pos : {0, 5, 100, 600, 1200}) {
		std::vector<uint8_t> bad = unhex(kLzma1);
		bad[pos % bad.size()] ^= 0x5A;
		out = decode(bad, false, props1, 0, &finished, &failed);
		CHECK(failed || out != plain1);
		bad = unhex(kLzma2);
		bad[pos % bad.size()] ^= 0x5A;
		out = decode(bad, true, dict2, 0, &finished, &failed);
		CHECK(failed || out != concat(t, std::vector<uint8_t>(t.rbegin(), t.rend())));
	}
	// Bad properties.
	LzmaDecoder d;
	MemorySource src;
	CHECK(!d.initLzma1(&src, 225, 4096) && d.failed());
	CHECK(!d.initLzma2(&src, 41) && d.failed());
}

void testPaths() {
	CHECK(InnoSetup::relativePath("{app}\\INTRO.STK") == "INTRO.STK");
	CHECK(InnoSetup::relativePath("{app}\\FDD\\DISK2.STK") == "FDD/DISK2.STK");
	CHECK(InnoSetup::relativePath("{app}\\webcache.zip", true) == "app/webcache.zip");
	CHECK(InnoSetup::relativePath("{tmp}\\a\\.\\b") == "tmp/a/b");
	CHECK(InnoSetup::relativePath("FDD\\INTRO.STK") == "FDD/INTRO.STK");
	CHECK(InnoSetup::relativePath("__support/app\\x.ini") == "__support/app/x.ini");
	CHECK(InnoSetup::relativePath("{app}\\..\\evil").empty());
	CHECK(InnoSetup::relativePath("a:b?.txt") == "a$b$.txt");
	CHECK(InnoSetup::relativePath("{{literal}") == "{literal}");
}

void testNotAnInstaller(const fs::path &scratch) {
	const fs::path junk = scratch / "junk.exe";
	FILE *f = std::fopen(junk.string().c_str(), "wb");
	if (f) {
		std::fputs("MZ this is not a program", f);
		std::fclose(f);
	}
	InnoSetup setup;
	CHECK(!setup.open(junk) && !setup.error().empty());
	CHECK(!setup.open(scratch / "missing.exe") && !setup.error().empty());
	GameInstaller installer;
	CHECK(!installer.open(junk) && installer.failed());
}

void testInstallers(const fs::path &dir, const fs::path &scratch) {
	struct Case {
		const char *file;
		GameId game;
		std::vector<std::string> files; // what is installed, in installer order
	};
	const Case cases[] = {
	    {"setup_gobliiins_1.02_(20270).exe", GameId::Gob1, {"GOB.LIC", "INTRO.STK", "Manual.pdf", "Track1.mp3"}},
	    {"setup_gobliins_2_-_the_prince_buffoon_1.02_(20270).exe", GameId::Gob2,
	     {"Manual.pdf", "Track1.mp3", "INTRO.STK", "GOBNEW.LIC"}},
	    {"setup_goblins_quest_3_1.02_(20270).exe", GameId::Gob3,
	     {"MDOS.IMD", "EXT.STK", "Track1.mp3", "IMD.ITK", "TOT.STK", "INTRO.STK", "Manual.pdf", "USGOB3.ITK",
	      "MUS_GOB3.LIC"}},
	};
	int found = 0;
	for (const Case &c : cases) {
		const fs::path path = dir / c.file;
		std::error_code ec;
		if (!fs::exists(path, ec)) {
			std::printf("skipped: %s not found\n", path.string().c_str());
			continue;
		}
		found++;
		InnoSetup setup;
		CHECK(setup.open(path));
		CHECK(setup.versionString() == "5.5.7 (unicode)");
		CHECK(setup.compression() == InnoCompression::Lzma2);
		std::string reason;
		const GameInfo *g = identifyInstaller(setup, reason);
		CHECK(g && g->id == c.game);
		GameInstallPlan plan;
		std::string error;
		CHECK(planGameInstall(setup, {}, plan, error));
		std::vector<std::string> names;
		for (const auto &f : plan.files)
			names.push_back(f.path);
		std::vector<std::string> want = c.files, got = names;
		std::sort(want.begin(), want.end());
		std::sort(got.begin(), got.end());
		CHECK(got == want);
		InstallOptions floppy;
		floppy.floppy = true;
		CHECK(planGameInstall(setup, floppy, plan, error) && plan.files.size() > c.files.size());

		// A small real extraction (the .LIC file) in steps of 1 KB, then a
		// truncated copy of the installer fails cleanly.
		for (const auto &f : plan.files) {
			if (f.path.find(".LIC") == std::string::npos)
				continue;
			InnoExtractor x;
			const fs::path out = scratch / (std::string(g->key) + ".lic");
			CHECK(x.begin(setup, {{f.file, {out}, f.path}}));
			while (x.step(1024)) {
			}
			CHECK(x.done() && !x.failed() && x.filesDone() == 1);
			if (x.failed())
				std::printf("%s\n", x.error().c_str());
			CHECK(fs::file_size(out, ec) == f.size);
		}
	}
	if (found == 0)
		std::printf("skipped: no GOG installers in %s\n", dir.string().c_str());
}

} // namespace

// A zip made with Python's zipfile: a folder entry pack/, a deflated
// pack/GOB00/12.png, a stored pack/README.txt and a __MACOSX/ extra.
const char *const kZip =
	"504b030414000000000000002100000000000000000000000000050000007061636b2f504b0304140000000800000021"
	"50b68367ab1500000090000000110000007061636b2f474f4230302f31322e706e67cb48cdc9c957c8c04196e717e5a4"
	"70e192a5851a00504b030414000000000000002150e29c53a507000000070000000f0000007061636b2f524541444d45"
	"2e74787473746f7265640a504b030414000000000000002150399cfb0604000000040000001a0000005f5f4d41434f53"
	"582f7061636b2f2e5f524541444d452e7478746a756e6b504b0102140314000000000000002100000000000000000000"
	"0000000500000000000000000000008001000000007061636b2f504b0102140314000000080000002150b68367ab1500"
	"0000900000001100000000000000000000008001230000007061636b2f474f4230302f31322e706e67504b0102140314"
	"000000000000002150e29c53a507000000070000000f00000000000000000000008001670000007061636b2f52454144"
	"4d452e747874504b0102140314000000000000002150399cfb0604000000040000001a00000000000000000000008001"
	"9b0000005f5f4d41434f53582f7061636b2f2e5f524541444d452e747874504b05060000000004000400f7000000d700"
	"00000000";

std::string fileText(const fs::path &p) {
	std::ifstream f(p, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

void testZip(const fs::path &scratch) {
	const std::vector<uint8_t> zip = unhex(kZip);
	const fs::path file = scratch / "pack.zip", dest = scratch / "unzipped";
	{
		std::ofstream f(file, std::ios::binary);
		f.write(reinterpret_cast<const char *>(zip.data()), std::streamsize(zip.size()));
	}
	ZipExtractor z;
	CHECK(z.open(file));
	CHECK(z.entries().size() == 2);
	CHECK(z.topFolder() == "pack");
	CHECK(z.begin(dest));
	while (z.step(1)) {
	}
	CHECK(z.done() && z.error().empty());
	std::string png;
	for (int i = 0; i < 4; i++)
		png += "hello hello hello hello hello world\n";
	CHECK(fileText(dest / "GOB00" / "12.png") == png);
	CHECK(fileText(dest / "README.txt") == "stored\n");
	CHECK(!fs::exists(dest / "__MACOSX"));
	// Damaged data: a byte of the deflated file changed.
	std::vector<uint8_t> bad = zip;
	bad[0x60] ^= 0x55;
	{
		std::ofstream f(file, std::ios::binary | std::ios::trunc);
		f.write(reinterpret_cast<const char *>(bad.data()), std::streamsize(bad.size()));
	}
	ZipExtractor z2;
	CHECK(z2.open(file) && z2.begin(scratch / "bad"));
	while (z2.step(1 << 20)) {
	}
	CHECK(!z2.done() && !z2.error().empty());
	// Not a zip file.
	{
		std::ofstream f(file, std::ios::binary | std::ios::trunc);
		f << "not a zip file at all, just some text that is long enough";
	}
	ZipExtractor z3;
	CHECK(!z3.open(file) && !z3.error().empty());
}

int main(int argc, char **argv) {
	std::string dir = argc > 1 ? argv[1] : "";
	if (dir.empty())
		if (const char *env = std::getenv("GOB_SETUP_DIR"))
			dir = env;
	std::error_code ec;
	const fs::path scratch = fs::temp_directory_path(ec) / "gob2native-test-install";
	fs::create_directories(scratch, ec);

	testHashes();
	testLzma();
	testPaths();
	testNotAnInstaller(scratch);
	testZip(scratch);
	if (!dir.empty())
		testInstallers(dir, scratch);
	else
		std::printf("skipped: the installer tests (give the folder with the setup_*.exe files, or set GOB_SETUP_DIR)\n");
	fs::remove_all(scratch, ec);

	if (failures) {
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("install tests passed\n");
	return 0;
}
