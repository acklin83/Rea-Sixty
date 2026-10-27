// The MIDI CC layer in bindings.json (plan docs/midi-cc-layer-plan.md, Baustein C):
// a layer's kind and CC set survive a save and a reload, a layer switched back to
// Reaper keeps its CC set, presets save, load and delete, and ccLayerActive follows
// the layer the surface is on. Runs the real bindings engine on a temp directory.

#include "Bindings.h"
#include "BindingsHost.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace bnd = uf8::bindings;

static int g_fail = 0;
#define EXPECT(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static std::string g_dir;

static std::string fileText()
{
    std::ifstream f(g_dir + "/bindings.json");
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

int main()
{
    char tmpl[] = "/tmp/rs_midiccXXXXXX";
    g_dir = mkdtemp(tmpl);
    bnd::Host h;
    h.configDir  = [] { return g_dir; };
    h.configFile = [] { return std::string("bindings.json"); };
    bnd::setHost(std::move(h));
    bnd::load();

    // Factory: every layer is a Reaper layer, nothing written about CC.
    for (int l = 0; l < 3; ++l) EXPECT(bnd::layerKind(l) == bnd::LayerKind::Reaper);
    EXPECT(!bnd::ccLayerActive());
    EXPECT(fileText().find("midi_cc") == std::string::npos);

    // Layer 2 becomes a CC layer with a hand-made set.
    bnd::setLayerKind(1, bnd::LayerKind::MidiCc);
    midicc::Layer cc = bnd::ccLayer(1);
    EXPECT(cc.pages[0].fader[0].number == 1);            // starts from the factory set
    cc.outputs = {"", "Fireface UFX+ MIDI"};
    cc.inputDevice = "Fireface UFX+ MIDI";
    cc.followItems = false;
    cc.defaultChannel = 5;
    cc.pages[3].fader[7].number = 31;
    cc.pages[3].fader[7].hiRes = true;
    cc.pages[3].fader[7].channel = 12;
    cc.pages[3].fader[7].name = "Fine \"q\"";
    cc.pages[3].fader[7].colour = 6;
    cc.pages[4].foot[1].number = 66;
    cc.pages[4].foot[1].behaviour = midicc::Behaviour::Hold;
    cc.pages[0].vpot[2].number = -1;                     // unassigned survives too
    bnd::setCcLayer(1, cc);

    // Not on it yet.
    EXPECT(!bnd::ccLayerActive());
    bnd::setActiveLayer(1);
    EXPECT(bnd::ccLayerActive() && bnd::ccLayerActiveIndex() == 1);
    bnd::setActiveLayer(0);
    EXPECT(!bnd::ccLayerActive());
    bnd::setActiveLayer(1);

    // Reload from disk.
    bnd::load();
    bnd::refreshCcLayerActive();
    EXPECT(bnd::layerKind(1) == bnd::LayerKind::MidiCc);
    EXPECT(bnd::layerKind(0) == bnd::LayerKind::Reaper);
    EXPECT(bnd::ccLayerActive() && bnd::ccLayerActiveIndex() == 1);
    const midicc::Layer r = bnd::ccLayer(1);
    EXPECT(r.outputs.size() == 2 && r.outputs[0].empty() && r.outputs[1] == "Fireface UFX+ MIDI");
    EXPECT(r.inputDevice == "Fireface UFX+ MIDI");
    EXPECT(!r.followItems);
    EXPECT(r.defaultChannel == 5);
    EXPECT(r.pages[3].fader[7].number == 31 && r.pages[3].fader[7].hiRes);
    EXPECT(r.pages[3].fader[7].channel == 12 && r.pages[3].fader[7].colour == 6);
    EXPECT(r.pages[3].fader[7].name == "Fine \"q\"");
    EXPECT(r.pages[4].foot[1].number == 66 && r.pages[4].foot[1].behaviour == midicc::Behaviour::Hold);
    EXPECT(r.pages[0].vpot[2].number == -1);
    EXPECT(r.pages[0].fader[1].number == 11);            // untouched factory entries

    // Back to Reaper: the set stays, on disk too.
    bnd::setLayerKind(1, bnd::LayerKind::Reaper);
    EXPECT(!bnd::ccLayerActive());
    bnd::load();
    EXPECT(bnd::layerKind(1) == bnd::LayerKind::Reaper);
    EXPECT(bnd::ccLayer(1).pages[3].fader[7].number == 31);
    bnd::setLayerKind(1, bnd::LayerKind::MidiCc);

    // Presets.
    EXPECT(bnd::saveCcPreset("Strings", 1));
    EXPECT(!bnd::saveCcPreset("", 1));
    EXPECT(bnd::loadCcPreset("Strings", 2));
    EXPECT(bnd::ccLayer(2).pages[3].fader[7].number == 31);
    EXPECT(bnd::layerKind(2) == bnd::LayerKind::Reaper);  // loading does not change the kind
    bnd::load();
    EXPECT(bnd::ccPresetNames().size() == 1 && bnd::ccPresetNames()[0] == "Strings");
    cc = bnd::ccLayer(1);
    cc.defaultChannel = 9;
    bnd::setCcLayer(1, cc);
    EXPECT(bnd::saveCcPreset("Strings", 1));              // overwrite by name
    EXPECT(bnd::ccPresetNames().size() == 1);
    EXPECT(bnd::loadCcPreset("Strings", 0));
    EXPECT(bnd::ccLayer(0).defaultChannel == 9);
    EXPECT(!bnd::loadCcPreset("Nope", 0));
    EXPECT(bnd::deleteCcPreset("Strings"));
    EXPECT(!bnd::deleteCcPreset("Strings"));
    bnd::load();
    EXPECT(bnd::ccPresetNames().empty());

    std::system(("rm -rf " + g_dir).c_str());
    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("midi_cc_bindings: all passed\n");
    return 0;
}
