;; CataGui window layout. Edit freely: the app reloads this file when it
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
;;            stone_white gear folder save play pause chart menu check
;;   (panel NAME)  game_info winrate_summary winrate_graph variation_tree
;;                 candidates move_list report compare comment engine_log
;;   (tabs NAME... :grow)  one panel at a time in a fixed box
;;   (menu TITLE-KEY COMMAND... - COMMAND... :fallback "Text")   inside menubar

(app
  (menubar
    (menu menu.file
      file.new file.open file.save file.save_as :fallback "File")
    (menu menu.game
      move.pass move.delete - game.ai_black game.ai_white game.ai_move game.match - game.score game.write_result :fallback "Game")
    (menu menu.navigate
      nav.first nav.back10 nav.back nav.forward nav.forward10 nav.last - nav.prev_variation nav.next_variation :fallback "Navigate")
    (menu menu.analysis
      analysis.toggle analysis.play_best analysis.game analysis.region - analysis.candidates analysis.ownership analysis.human_policy - engine.second engine.restart engine.setup :fallback "Analysis")
    (menu menu.view
      view.coordinates view.move_numbers view.next_tab - view.zoom_in view.zoom_out - view.settings :fallback "View"))

  (toolbar
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
    (button game.ai_white :icon "@stone_white AI")
    (spacer)
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
