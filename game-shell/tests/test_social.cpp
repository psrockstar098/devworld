// tests for src/social.h — contacts, messaging and teams (Phase 1, offline).
#include "doctest.h"
#include "social.h"
#include "json.h"

using namespace dw;

static Social sample() {
  Social s;
  s.load(parseJson(R"({"contacts":[
    {"id":"nova","name":"Nova","role":"Mentor","cityId":"starter_town",
     "skill":"Python basics","blurb":"Teaches loops the hard way."},
    {"id":"kernel","name":"Kernel","role":"Infra","cityId":"systems_district",
     "skill":"Linux and networks","blurb":"Sleeps in a terminal."},
    {"id":"ada","name":"Ada","role":"PM","cityId":"startup_city",
     "skill":"Product","blurb":"Turns mud into roadmaps."}
  ]})"));
  return s;
}

TEST_CASE("contacts load and are addressable") {
  Social s = sample();
  REQUIRE(s.contacts().size() == 3);
  REQUIRE(s.contact("nova") != nullptr);
  CHECK(s.contact("nova")->skill == "Python basics");
  CHECK(s.contact("ghost") == nullptr);
}

TEST_CASE("replies are deterministic and react to keywords") {
  Social s = sample();
  const ContactDef& nova = *s.contact("nova");
  CHECK(Social::reply(nova, "How do I write a loop?") ==
        Social::reply(nova, "How do I write a loop?"));  // same in, same out
  CHECK(Social::reply(nova, "help").find("error") != std::string::npos);
  CHECK(Social::reply(nova, "stuck on tests").find("Error") != std::string::npos);
  CHECK(Social::reply(nova, "can you join my team").find("team") != std::string::npos);
  CHECK(Social::reply(nova, "hi").find("Nova") != std::string::npos);
  CHECK(Social::reply(nova, "thanks!").find("Anytime") != std::string::npos);
  CHECK(Social::reply(nova, "random musing").find("starter_town") != std::string::npos);
}

TEST_CASE("an exchange records both sides and marks unread") {
  Social s = sample();
  Player p;
  Social::exchange(p, *s.contact("kernel"), "hey", "2026-01-01T00:00");
  REQUIRE(p.messages.size() == 2);
  CHECK(p.messages[0].fromThem == false);
  CHECK(p.messages[1].fromThem == true);
  CHECK(p.messages[1].contactId == "kernel");
  CHECK(Social::unreadFrom(p, "kernel") == 1);
  CHECK(Social::unreadCount(p) == 1);

  Social::markRead(p, "kernel");
  CHECK(Social::unreadCount(p) == 0);
}

TEST_CASE("threads are per contact") {
  Social s = sample();
  Player p;
  Social::exchange(p, *s.contact("nova"), "a", "t1");
  Social::exchange(p, *s.contact("ada"), "b", "t2");
  CHECK(Social::thread(p, "nova").size() == 2);
  CHECK(Social::thread(p, "ada").size() == 2);
  CHECK(Social::thread(p, "kernel").empty());
}

TEST_CASE("the message log is capped") {
  Social s = sample();
  Player p;
  for (int i = 0; i < 20; ++i) Social::exchange(p, *s.contact("ada"), "msg", "t", 10);
  CHECK(p.messages.size() == 10);
  CHECK(p.messages.back().at == "t");
}

TEST_CASE("teams cap at three, reject duplicates, and count known members") {
  Social s = sample();
  Player p;
  CHECK(Social::addToTeam(p, "nova"));
  CHECK(Social::addToTeam(p, "kernel"));
  CHECK(Social::addToTeam(p, "ada"));
  std::string why;
  CHECK_FALSE(Social::addToTeam(p, "nova", &why));   // duplicate
  CHECK(why.find("Already") != std::string::npos);
  CHECK(Social::teamSkill(p, s.contacts()) == 3);

  CHECK(Social::removeFromTeam(p, "ada"));
  CHECK_FALSE(Social::removeFromTeam(p, "ada"));
  CHECK(Social::teamSkill(p, s.contacts()) == 2);
}

TEST_CASE("teams reject a fourth member and unknown contacts do not count") {
  Social s = sample();
  Player p;
  Social::addToTeam(p, "nova");
  Social::addToTeam(p, "kernel");
  Social::addToTeam(p, "ada");
  std::string why;
  CHECK_FALSE(Social::addToTeam(p, "extra", &why));
  CHECK(why.find("full") != std::string::npos);

  Player q;
  Social::addToTeam(q, "nobody");               // allowed by id, but
  CHECK(Social::teamSkill(q, s.contacts()) == 0);  // counts only known contacts
}
