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
    (choice rules :columns 3 :label label.rules :fallback Rules
      (option chinese Chinese)
      (option japanese Japanese)
      (option korean Korean)
      (option aga AGA)
      (option new-zealand "New Zealand")
      (option tromp-taylor Tromp-Taylor))
    (number ai.seconds_per_move :max 60 :step 0.5 :format "%.1f s" :label settings.ai_time :fallback "Engine time per move"))
  (section settings.analysis :fallback Analysis
    (bool analysis.on_start :label settings.analyze_on_start :fallback "Analyze on start")
    (bool analysis.show_candidates :label command.analysis.candidates :fallback Candidates)
    (bool analysis.show_ownership :label command.analysis.ownership :fallback Territory)
    (number analysis.max_candidates :step 1 :format "%.0f" :label settings.max_candidates :fallback "Candidates shown")
    (number analysis.interval_cs :step 5 :format "%.0f cs" :label settings.interval :fallback "Update interval"))
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
    (text new_game.white :placeholder White :label label.white :fallback White)))
