#pragma once

#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// The word list, and the review that makes it worth keeping.
//
// Two modes, deliberately in that order: if anything is due, the screen opens
// on a review rather than a list, because the retrieval is the part that works
// and a list is the part that feels productive. The queue is finite and
// empties; there is no count anywhere else in the firmware nagging about it.
//
// A review shows the word alone and asks you to recall it. Only then does the
// book it came from appear, and you grade yourself. Nothing is scored or kept
// beyond the schedule.
class WordListActivity final : public Activity {
 public:
  WordListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Mode : uint8_t {
    Review,    // a due word, hidden context, waiting for recall
    Revealed,  // context shown, waiting for the grade
    Browse,    // the whole list
    Done,      // queue emptied this session
  };

  Mode mode = Mode::Review;
  int32_t today = -1;

  // Copied out of the store so grading, which rewrites the vector, cannot
  // invalidate what the screen is holding.
  std::vector<std::string> queue;
  size_t queueIndex = 0;
  std::string currentWord;
  std::string currentBook;

  int browseIndex = 0;
  int browseTop = 0;
  ButtonNavigator buttonNavigator;

  void loadQueue();
  bool advance();
  void gradeCurrent(bool remembered);

  void renderReview(bool revealed) const;
  void renderBrowse() const;
  void renderDone() const;
};
