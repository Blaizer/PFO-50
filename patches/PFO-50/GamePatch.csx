/*
PFO 50 - Play Forever Online 50 patching script
*/

#load "../../patcher/lib/_Utils.csx"
#load "../../patcher/lib/_Patch.csx"
#load "../../patcher/lib/_UFO50.csx"
#load "_Extension.csx"

using System.Threading.Tasks;

var ufo50Version = GetUFO50Version(Data);
var expectedUfo50Version = GetGameVersion();
var scriptDir = Path.GetDirectoryName(GetCurrentScript());

PatchPFO50Extension();

ImportSprites();

await ImportCode();

await ApplyCompatibleCodePatch(ufo50Version, scriptDir, new[] {
    new PatchVersionRange(expectedUfo50Version, expectedUfo50Version)
}, true);
