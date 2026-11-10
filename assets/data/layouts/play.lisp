;; Layout preset (Customize layout -> Presets). Same format as data/layout.lisp.
;; Play: a quiet board for games against the engine (no analysis panels).

(app
  (menubar
    (menu menu.file
      file.new file.open file.save file.save_as :fallback "File")
    (menu menu.game
      game.info - move.pass move.delete - game.ai_black game.ai_white game.ai_move game.match - game.score game.write_result - sync.select sync.toggle :fallback "Game")
    (menu menu.navigate
      nav.first nav.back10 nav.back nav.forward nav.forward10 nav.last - nav.prev_variation nav.next_variation :fallback "Navigate")
    (menu menu.analysis
      analysis.toggle analysis.play_best analysis.game analysis.report analysis.rank analysis.region analysis.tsumego_frame - analysis.candidates analysis.ownership analysis.human_policy - engine.second engine.restart engine.setup :fallback "Analysis")
    (menu menu.view
      view.coordinates view.move_numbers view.next_tab - view.zoom_in view.zoom_out - view.theme_editor view.layout_editor view.settings :fallback "View"))

  (toolbar
    (button file.new :icon @plus :label)
    (button file.open :icon @folder :label)
    (button file.save :icon @save :label)
    (separator)
    (button game.ai_black :icon "@stone_black AI")
    (button game.ai_white :icon "@stone_white AI")
    (button game.ai_move)
    (spacer)
    (engine-status)
    (button view.settings :icon @gear))

  (row :grow
    (board :grow)
    (column :width 340 :scroll :min-height 640 :padding 12 :gap 12
      (panel game_info)
      (tabs move_list comment variation_tree :grow)))

  (navbar
    (button nav.first :icon @first)
    (button nav.back10 :icon @prev2)
    (button nav.back :icon @prev)
    (move-counter)
    (button nav.forward :icon @next)
    (button nav.forward10 :icon @next2)
    (button nav.last :icon @last)
    (separator)
    (button nav.prev_variation :icon @up)
    (button nav.next_variation :icon @down)
    (spacer)
    (status)
    (button game.score)
    (button move.pass)))
