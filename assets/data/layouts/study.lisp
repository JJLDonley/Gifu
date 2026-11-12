;; Layout preset (Customize layout -> Presets). Same format as data/layout.lisp.
;; Study: comments and the variation tree first, sidebar on the left.

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
    (menu menu.markup
      toolbar.markup markup.tool.play markup.tool.black markup.tool.white - markup.tool.triangle markup.tool.square markup.tool.circle markup.tool.cross markup.tool.letter markup.tool.number - markup.tool.arrow markup.tool.line markup.tool.pen - markup.tool.shade markup.count_empty - markup.tool.eraser markup.clear :fallback "Markup")
    (menu menu.view
      view.coordinates view.move_numbers view.branches view.next_tab - toolbar.analysis toolbar.markup - view.zoom_in view.zoom_out - view.theme_editor view.layout_editor view.settings :fallback "View"))

  (toolbar
    (mode-switch)
    (separator)
    (modes :grow
      (mode analysis :label toolbar.analysis :fallback "Analysis"
        (button file.new :icon @plus :label)
        (button file.open :icon @folder :label)
        (button file.save :icon @save :label)
        (separator)
        (button analysis.toggle :style primary)
        (button analysis.game :icon @chart)
        (button analysis.candidates)
        (button analysis.ownership)
        (button analysis.human_policy)
        (button view.coordinates)
        (separator)
        (button game.ai_black :icon "@stone_black AI")
        (button game.ai_white :icon "@stone_white AI"))
      (mode markup :label toolbar.markup :fallback "Markup"
        (button markup.tool.play :icon @play_stones)
        (button markup.tool.black :icon @stone_black)
        (button markup.tool.white :icon @stone_white)
        (separator)
        (button markup.tool.triangle :icon @triangle)
        (button markup.tool.square :icon @square)
        (button markup.tool.circle :icon @circle)
        (button markup.tool.cross :icon @cross)
        (button markup.tool.letter :icon @letter)
        (button markup.tool.number :icon @number)
        (separator)
        (button markup.tool.arrow :icon @arrow)
        (button markup.tool.line :icon @line)
        (button markup.tool.pen :icon @pen)
        (separator)
        (button markup.tool.shade :icon @shade)
        (markup-colors)
        (markup-counts)
        (separator)
        (button markup.tool.eraser :icon @eraser)
        (button markup.clear :icon @trash)))
    (engine-status)
    (button view.settings :icon @gear))

  (row :grow
    (column :width 420 :scroll :min-height 640 :padding 12 :gap 12
      (panel game_info)
      (panel comment)
      (panel variation_tree)
      (tabs move_list candidates winrate_graph report :grow))
    (board :grow))

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
