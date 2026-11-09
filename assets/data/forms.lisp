;; Data-driven forms: the settings and new-game dialogs.
;;   (form NAME :title I18N-KEY :fallback "Text" (section ...) ...)
;;   (section TITLE-KEY :fallback "Text" field...)
;; Fields bind to config keys (the field table in src/app/config.c):
;;   (bool KEY :label I18N-KEY :fallback "Text")
;;   (number KEY :min N :max N :step N :format "%.1f" ...)   range defaults to the field's
;;   (choice KEY :columns N [:source themes|engines] (option VALUE "Label") ...)
;;   (text KEY :placeholder "Text" ...)
;; Changes apply immediately (settings) or when the dialog's action runs.

(form settings :title command.view.settings :fallback Settings
  (section settings.appearance :fallback Appearance
    (choice theme :source themes :columns 2 :label label.theme :fallback Theme)
    (choice language :columns 3 :label settings.language :fallback Language
      (option en English)
      (option zh_CN "简体中文")
      (option zh_TW "繁體中文")
      (option ja "日本語")
      (option ko "한국어"))
    (number ui_scale :min 0.75 :max 2.0 :step 0.05 :format "%.2f×" :label settings.ui_scale :fallback "Interface scale"))
  (section settings.engine :fallback Engine
    (choice engine :source engines :columns 1 :label settings.engine_profile :fallback "Engine profile")
    (choice engine2 :source engines :columns 1 :label settings.engine2 :fallback "Second engine (compare / matches)")
    (choice rules :columns 3 :label label.rules :fallback Rules
      (option chinese Chinese)
      (option japanese Japanese)
      (option korean Korean)
      (option aga AGA)
      (option new-zealand "New Zealand")
      (option tromp-taylor Tromp-Taylor))
    (number ai.seconds_per_move :max 60 :step 0.5 :format "%.1f s" :label settings.ai_time :fallback "Engine time per move")
    (bool ai.human_style :label settings.human_style :fallback "AI plays like a human (needs the human network)")
    (number engine.max_processes :min 1 :max 4 :step 1 :format "%.0f" :label settings.max_processes :fallback "KataGo processes at once (GPU memory)")
    (choice ai.human_profile :columns 5 :label settings.human_profile :fallback "Human strength (moves overlay, AI play)"
      (option rank_20k 20k) (option rank_15k 15k) (option rank_10k 10k) (option rank_5k 5k) (option rank_1k 1k)
      (option rank_1d 1d) (option rank_3d 3d) (option rank_5d 5d) (option rank_9d 9d) (option proyear_2023 "Pro 2023")
      ; Pre-AlphaZero style (human play before 2016) and professionals of a given year.
      (option preaz_15k "Pre-AZ 15k") (option preaz_5k "Pre-AZ 5k") (option preaz_1d "Pre-AZ 1d") (option preaz_5d "Pre-AZ 5d") (option preaz_9d "Pre-AZ 9d")
      (option proyear_1800 "Pro 1800") (option proyear_1900 "Pro 1900") (option proyear_1950 "Pro 1950") (option proyear_1990 "Pro 1990") (option proyear_2015 "Pro 2015")))
  (section settings.analysis :fallback Analysis
    (bool analysis.on_start :label settings.analyze_on_start :fallback "Analyze on start")
    (bool analysis.show_candidates :label command.analysis.candidates :fallback Candidates)
    (bool analysis.show_ownership :label command.analysis.ownership :fallback Territory)
    (bool analysis.show_human_policy :label command.analysis.human_policy :fallback "Human moves")
    (number analysis.max_candidates :step 1 :format "%.0f" :label settings.max_candidates :fallback "Candidates shown")
    (number analysis.interval_cs :step 5 :format "%.0f cs" :label settings.interval :fallback "Update interval")
    (number analysis.game_visits :min 50 :max 5000 :step 50 :format "%.0f" :label settings.game_visits :fallback "Game analysis visits"))
  (section settings.board :fallback Board
    (bool board.show_coordinates :label command.view.coordinates :fallback Coordinates)
    (bool board.show_move_numbers :label command.view.move_numbers :fallback "Move numbers")))

(form new_game :title command.file.new :fallback "New game"
  (section settings.board :fallback Board
    (choice new_game.size :columns 4 :label new_game.size :fallback "Board size"
      (option 9 "9 × 9")
      (option 13 "13 × 13")
      (option 19 "19 × 19")
      (option 25 "25 × 25"))
    (number new_game.size :step 1 :format "%.0f" :label new_game.custom_size :fallback "Custom size")
    (number new_game.handicap :step 1 :format "%.0f" :label new_game.handicap :fallback Handicap)
    (number new_game.komi :step 0.5 :format "%.1f" :label label.komi :fallback Komi)
    (choice rules :columns 3 :label label.rules :fallback Rules
      (option chinese Chinese)
      (option japanese Japanese)
      (option korean Korean)
      (option aga AGA)
      (option new-zealand "New Zealand")
      (option tromp-taylor Tromp-Taylor)))
  (section new_game.players :fallback Players
    (text new_game.black :placeholder Black :label label.black :fallback Black)
    (text new_game.white :placeholder White :label label.white :fallback White))
  (section new_game.opponent_section :fallback Opponent
    (choice new_game.opponent :columns 3 :label new_game.opponent :fallback "Engine plays"
      (option none "Nobody")
      (option white "White")
      (option black "Black"))
    (bool ai.human_style :label settings.human_style :fallback "AI plays like a human (needs the human network)")
    (choice ai.human_profile :columns 5 :label new_game.opponent_rank :fallback "Opponent strength (human style)"
      (option rank_20k 20k) (option rank_15k 15k) (option rank_10k 10k) (option rank_7k 7k) (option rank_5k 5k)
      (option rank_3k 3k) (option rank_1k 1k) (option rank_1d 1d) (option rank_3d 3d) (option rank_5d 5d)
      (option rank_7d 7d) (option rank_9d 9d) (option proyear_2023 "Pro 2023"))))

;; Game info: edits the SGF root (PB PW BR WR KM RE EV DT); Apply writes it back.
;; A Fox-style komi (375 = 3.75 zi) typed here is converted like on load.
(form game_info :title game_info.title :fallback "Game info"
  (section new_game.players :fallback Players
    (text game_info.black :placeholder Black :label label.black :fallback Black)
    (text game_info.black_rank :placeholder "e.g. 5d" :label game_info.black_rank :fallback "Black rank")
    (text game_info.white :placeholder White :label label.white :fallback White)
    (text game_info.white_rank :placeholder "e.g. 3k" :label game_info.white_rank :fallback "White rank"))
  (section game_info.game :fallback Game
    (number game_info.komi :step 0.5 :format "%.1f" :label label.komi :fallback Komi)
    (text game_info.result :placeholder "e.g. B+R, W+3.5" :label game_info.result :fallback Result)
    (text game_info.event :placeholder "" :label game_info.event :fallback Event)
    (text game_info.date :placeholder "YYYY-MM-DD" :label game_info.date :fallback Date)))
