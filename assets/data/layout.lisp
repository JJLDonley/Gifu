;; Gifu window layout. Edit freely: the app reloads this file when it
;; changes. A copy in <config_dir>/layout.lisp overrides this one.
;;
;;   (component positional... :prop value :flag (child ...) ...)
;;
;; Positional args come first: after the first :keyword, an atom is that
;; keyword's value.
;;
;; Containers: app row column toolbar navbar menubar tabs
;;   :grow          take the leftover space along the parent's axis
;;   :width N  :height N  :gap N  :padding N
;;   :scroll        (column) scroll vertically; :min-height N
;; Widgets: button separator spacer engine-status theme-picker move-counter
;;          status board panel
;;   (button COMMAND :icon @name :label :style primary|ghost)
;;     icons: first prev next last prev2 next2 up down close plus stone_black
;;            stone_white gear folder save play pause chart menu check help rank clipboard
;;            screen triangle square circle cross letter number arrow line pen shade
;;            eraser trash play_stones
;;   (panel NAME)  game_info winrate_summary winrate_graph variation_tree
;;                 candidates move_list report compare comment engine_log
;;                 territory confidence human_winrate
;;   (tabs NAME... :grow)  one panel at a time in a fixed box
;;   (modes (mode NAME :label KEY :fallback "Text" item...) ...)   inside toolbar: one row
;;     of items at a time; (mode-switch) switches (also toolbar.next, M); a mode named
;;     "markup" turns the board into a drawing surface
;;   (markup-colors) (markup-counts)   the shading palette and the shaded points per colour
;;   (menu TITLE-KEY COMMAND... - COMMAND... :fallback "Text")   inside menubar
;;   A copy in <config_dir> still gets the menus from this file (so new commands
;;   show up); write (menubar :custom ...) there to keep your own menus.

(app
  (menubar
    (menu menu.file
      file.new file.open file.save file.save_as :fallback "File")
    (menu menu.edit
      edit.undo edit.redo - move.delete :fallback "Edit")
    (menu menu.game
      game.info game.komi - move.pass move.delete - game.ai_black game.ai_white game.ai_move game.match game.show_eval - game.score game.write_result - sync.select sync.toggle :fallback "Game")
    (menu menu.navigate
      nav.first nav.back10 nav.back nav.forward nav.forward10 nav.last - nav.prev_variation nav.next_variation :fallback "Navigate")
    (menu menu.analysis
      analysis.toggle analysis.play_best analysis.game analysis.report analysis.profile analysis.rank analysis.region analysis.tsumego_frame - analysis.candidates analysis.ownership analysis.human_policy - engine.second engine.restart engine.setup :fallback "Analysis")
    (menu menu.markup
      toolbar.markup markup.tool.play markup.tool.black markup.tool.white - markup.tool.triangle markup.tool.square markup.tool.circle markup.tool.cross markup.tool.letter markup.tool.number - markup.tool.arrow markup.tool.line markup.tool.pen - markup.tool.shade markup.count_empty - markup.tool.eraser markup.clear :fallback "Markup")
    (menu menu.view
      view.coordinates view.move_numbers view.branches view.next_tab - toolbar.analysis toolbar.markup - view.zoom_in view.zoom_out - view.theme_editor view.layout_editor view.settings :fallback "View")
    (menu menu.help
      help.check_updates help.website :fallback "Help"))

  ;; Right-click on the board: an empty point, or a stone played as a move. Edits
  ;; replay the game from that move (later moves that become illegal are left out).
  (board-menu empty
    edit.insert_black edit.insert_white - edit.undo edit.redo)
  (board-menu stone
    edit.jump_to_move edit.move_stone edit.swap_color edit.delete_move - edit.undo edit.redo)

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
    (board :grow)
    (column :width 400 :scroll :min-height 640 :padding 12 :gap 12
      (panel game_info)
      (panel winrate_summary)
      (panel winrate_graph :height 150)
      (tabs candidates move_list report compare variation_tree comment :grow)))

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
