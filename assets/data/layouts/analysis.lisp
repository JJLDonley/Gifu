;; Layout preset (Customize layout -> Presets). Same format as data/layout.lisp.
;; Analysis: the default — live analysis, graph, report and tabs.

(app
  (menubar
    (menu menu.file
      file.new file.open file.save file.save_as :fallback "File")
    (menu menu.game
      game.info - move.pass game.resign move.delete - game.ai_black game.ai_white game.ai_move game.match - game.score game.write_result - sync.select sync.toggle :fallback "Game")
    (menu menu.navigate
      nav.first nav.back10 nav.back nav.forward nav.forward10 nav.last - nav.prev_variation nav.next_variation :fallback "Navigate")
    (menu menu.analysis
      analysis.toggle analysis.play_best analysis.game analysis.report analysis.rank analysis.region analysis.tsumego_frame - analysis.candidates analysis.ownership analysis.human_policy - engine.second engine.restart engine.setup :fallback "Analysis")
    (menu menu.markup
      toolbar.markup markup.tool.play markup.tool.black markup.tool.white - markup.tool.triangle markup.tool.square markup.tool.circle markup.tool.cross markup.tool.letter markup.tool.number - markup.tool.arrow markup.tool.line markup.tool.pen - markup.tool.shade markup.count_empty - markup.tool.eraser markup.clear :fallback "Markup")
    (menu menu.view
      view.coordinates view.move_numbers view.branches view.next_tab - toolbar.analysis toolbar.markup - view.zoom_in view.zoom_out - view.theme_editor view.layout_editor view.console view.settings :fallback "View"))

  (toolbar
    (mode-switch)
    (separator)
    (modes :grow
      (mode analysis :label toolbar.analysis :fallback "Analysis"
        (button file.new)
        (button file.open)
        (button file.save)
        (separator)
        (button analysis.toggle :style primary)
        (button analysis.game)
        (button analysis.candidates)
        (button analysis.ownership)
        (button analysis.human_policy)
        (button view.coordinates)
        (button view.move_numbers)
        (separator)
        (button game.ai_black)
        (button game.ai_white))
      (mode markup :label toolbar.markup :fallback "Markup"
        (button markup.tool.play)
        (button markup.tool.black)
        (button markup.tool.white)
        (separator)
        (button markup.tool.triangle)
        (button markup.tool.square)
        (button markup.tool.circle)
        (button markup.tool.cross)
        (button markup.tool.letter)
        (button markup.tool.number)
        (separator)
        (button markup.tool.arrow)
        (button markup.tool.line)
        (button markup.tool.pen)
        (separator)
        (button markup.tool.shade)
        (markup-colors)
        (markup-counts)
        (separator)
        (button markup.tool.eraser)
        (button markup.clear)))
    (engine-status)
    (button view.settings))

  (row :grow
    (board :grow)
    (column :width 400 :scroll :min-height 640 :padding 12 :gap 12
      (panel game_info)
      (panel winrate_summary)
      (panel winrate_graph :height 150)
      (tabs candidates move_list report compare variation_tree comment :grow)))

  (navbar
    (button nav.first)
    (button nav.back10)
    (button nav.back)
    (move-counter)
    (button nav.forward)
    (button nav.forward10)
    (button nav.last)
    (separator)
    (button nav.prev_variation)
    (button nav.next_variation)
    (spacer)
    (status)
    (button game.score)
    (button move.pass)))
