#pragma once
// game.h — the DEVWORLD game shell orchestrator (C++17, SDL2, Emscripten).
//
// Screens: character creation (DOM) -> world (canvas) with DOM panels.
// The coding workspace is the real VS Code IDE in an overlay iframe;
// this class bridges to it (bridge.h) but never reimplements it.

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>
#include <map>
#include "types.h"
#include "world.h"
#include "world_model.h"
#include "audio.h"
#include "equipment.h"
#include "profile.h"
#include "regions.h"
#include "dungeons.h"
#include "housing.h"
#include "social.h"
#include "portfolio.h"
#include "company.h"
#include "json.h"

namespace dw {

class Game {
 public:
  bool init(int vw, int vh);
  void frame();                       // called by the emscripten main loop
  void onKeyDown(SDL_Keycode k);
  void onKeyUp(SDL_Keycode k);
  void shutdown();

  // ---- JS -> C++ entry points (called via ccall) ----
  void uiEvent(const std::string& json);       // panel actions from shell.js
  void missionTestResult(int passed, int total);
  void setKeybind(const std::string& action, int keycode);
  void touchMove(const std::string& dir, bool down);  // on-screen D-pad
  void queueSeed(const std::string& missionId);  // IDE not loaded yet; retry later
  void ideReady();                               // iframe loaded: flush seed queue

  Player& player() { return player_; }
  std::string panelJson(const std::string& panel);  // shop/bank/inventory/map/skill/hud

 private:
  // sdl
  SDL_Window* win_ = nullptr;
  SDL_Renderer* ren_ = nullptr;
  TTF_Font* font_ = nullptr;
  int vw_ = 960, vh_ = 600;

  // state
  Player player_;
  World world_;
  WorldGraph graph_;
  AmbienceAudio audio_;
  std::vector<NpcDef> npcs_;
  std::map<std::string, MissionDef> missions_;
  std::vector<ItemDef> shopItems_;
  std::map<std::string, ItemDef> catalog_;   // itemId -> def (equipment + panels)
  Regions regions_;                          // regional economies (city directory)
  Dungeons dungeons_;                        // hidden technology dungeons
  Housing housing_;                          // developer homes
  Social social_;                            // contacts, messaging, teams
  std::string curMap_ = "town";
  Vec2 cam_{0, 0};
  float dayT_ = 0.72f;          // warm dusk at start (world spec vibe)
  double animT_ = 0;
  bool hasSave_ = false;
  bool uiOpen_ = false;         // any DOM panel open (pauses world input)
  std::string pendingMission_;  // mission awaiting test results
  std::vector<std::string> pendingSeeds_;  // missions to seed when IDE loads

  // input
  std::map<std::string, SDL_Keycode> keys_;  // action -> key
  std::map<SDL_Keycode, bool> held_;
  bool moveUp_ = false, moveDown_ = false, moveLeft_ = false, moveRight_ = false;

  // content
  void loadContent();
  void loadNpcs(const JVal& j);
  void loadMissions(const JVal& j);
  void loadShop(const JVal& j);

  // loop
  void update(float dt);
  void render();
  void updateCamera();
  void updateNpcs();

  // interaction
  struct Hit { std::string kind, id, label, target; Vec2 at{0,0}; };
  bool findInteract(Hit& out);           // nearest interactable/npc/door in range
  void doInteract(const Hit& h);
  void enterMap(const std::string& mapId, int tx, int ty);
  Vec2 npcPos(const NpcDef& n) const;    // px, from schedule

  // missions / economy
  void acceptMission(const std::string& id);
  void completeMission(const std::string& id, int passed, int total);
  void addXp(int64_t xp);
  void ledgerAdd(const std::string& kind, const std::string& memo, int64_t delta);
  void buyItem(const std::string& id);
  std::string missionsJson();            // for the board panel
  std::string questJson();               // quest log
  std::string travelJson();              // bus stop / fast travel panel
  std::string npcDialogueJson(const std::string& npcId, const std::string& nodeId);

  // identity / interaction (2.2)
  void notice(const std::string& text, const std::string& kind = "info");  // toast + log
  void equipItem(const std::string& itemId);
  void unequipSlot(const std::string& slot);
  void trackMission(const std::string& id);       // set/clear the HUD objective
  void applySettings(const JVal& j);              // accessibility + keybinds
  std::string profileJson();                      // profile panel
  std::string settingsJson();                     // settings panel (a11y state)
  std::string notificationsJson();                // notification log
  std::string examineJson(const std::string& id); // inspect an object

  // world / open-world (2.3)
  bool hatchRevealed(const Interactable& it) const;   // dungeon found?
  const DungeonDef* dungeonForInterior(const std::string& mapId) const;
  void grantDungeonReward(const DungeonDef& d);        // rarity on mission clear
  std::string portfolioJson();                         // project portfolio
  std::string companyJson();                           // company dashboard
  std::string contactsJson(const std::string& contactId);  // friends / messaging
  std::string homeJson();                              // home decoration panel
  void placeHome(const std::string& slot, const std::string& item);
  void clearHome(const std::string& slot);
  void toggleTrophy(const std::string& achievement);
  void sendMessage(const std::string& contactId, const std::string& text);
  void teamChange(const std::string& contactId, bool add);
  void hireContact(const std::string& contactId, bool add);  // company roster
  void foundCompany(const std::string& name, const std::string& focus);
  void revealDungeon(const std::string& flag);         // rumour -> discovery

  // world / travel
  std::string currentCityId() const;
  const CityDef* currentCity() const;
  void travelTo(const std::string& cityId);   // validates unlock + fare
  void drawMinimap();                          // on-screen navigation aid

  // persistence
  void saveNow();
  void loadOrCreate();

  // helpers
  bool collides(float x, float y) const;  // player bbox at (x,y)
  void drawPrompt(const std::string& text);};

extern Game* g_game;  // set in main.cpp

}  // namespace dw
