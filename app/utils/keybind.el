;;;; Funcionalidades varias

(defun clm:adjust-number-at-point (delta)
  "Adjust the integer at point by DELTA."
  (save-excursion
    (skip-chars-backward "-+0-9")
    (when (looking-at "[-+]?[0-9]+")
      (let* ((beg (match-beginning 0))
             (end (match-end 0))
             (num (string-to-number (match-string 0)))
             (new-num (+ num delta)))
        (delete-region beg end)
        (goto-char beg)
        (insert (number-to-string new-num))))))

(defun clm:increment-number-at-point (arg)
  "Increment integer at point by ARG."
  (interactive "p")
  (clm:adjust-number-at-point arg))

(defun clm:decrement-number-at-point (arg)
  "Decrement integer at point by ARG."
  (interactive "p")
  (clm:adjust-number-at-point (- arg)))

(defvar clm:block-target-file "../tmp/live.p"
  "Archivo destino donde se graba el bloque actual.")
;;; Cambiar con:
; (setq clm:block-target-file "./otro.p")

(defun clm:send-block-to-file ()
  "Toma el bloque de codigo alrededor del cursor, delimitado
por lineas que contienen solo '...', y lo graba en
`clm:block-target-file'."
  (interactive)
  (let* ((sep-re "^\\s-*\\.\\.\\.\\s-*$")
         (start
          (save-excursion
            (beginning-of-line)
            ;; Si el cursor esta sobre un separador, avanzar
            (when (looking-at sep-re)
              (forward-line 1))
            (if (re-search-backward sep-re nil t)
                (progn (forward-line 1) (point))
              (point-min))))
         (end
          (save-excursion
            (end-of-line)
            (if (re-search-forward sep-re nil t)
                (progn (beginning-of-line) (point))
              (point-max))))
         (block (string-trim
                 (buffer-substring-no-properties start end))))
    (if (string-empty-p block)
        (message "Bloque vacio, nada que enviar.")
      (with-temp-file clm:block-target-file
        (insert block "\n"))
      (message "Bloque enviado a %s (%d chars)"
               clm:block-target-file
               (length block)))))


;; ------------------------------------------------------------------
;; Volumen del sistema con el numpad: s-<kp-add> / s-<kp-subtract>.
;; Paso 2 normal, paso 10 con Shift. Funciona con la tecla mantenida
;; (auto-repeat) porque el cambio de volumen es asincrono.
;; ------------------------------------------------------------------

(defvar clm:volume-step 4
  "Paso de volumen sin Shift.")

(defvar clm:volume-step-shift 10
  "Paso de volumen con Shift.")

(defvar clm:volume-counter 0
  "Contador para nombrar los procesos de volumen.")


(defun clm:adjust-volume (delta)
  "Mueve el volumen DELTA puntos y luego informa el valor resultante."
  (let ((arg (format "%s%d%%" (if (> delta 0) "+" "-") (abs delta))))
    (start-process
     (format "clm-vol-%d" (setq clm:volume-counter (1+ clm:volume-counter)))
     nil "pactl" "set-sink-volume" "@DEFAULT_SINK@" arg)
    (run-with-timer 0.15 nil #'clm:volume-report)))

(defun clm:volume-default-sink ()
  "Devuelve el nombre del sink por defecto segun pactl info."
  (let ((out (shell-command-to-string "LC_ALL=C pactl info 2>/dev/null")))
    (when (and out (string-match "Default Sink:[ \t]*\\([^ \n]+\\)" out))
      (match-string 1 out))))

(defun clm:volume-report ()
  "Consulta el volumen del sink por defecto y lo muestra en el minibuffer."
  (let* ((def (clm:volume-default-sink))
         (out (shell-command-to-string "LC_ALL=C pactl list sinks 2>/dev/null"))
         (pos (if (and def out)
                  (string-match (concat "Name:[ \t]*" (regexp-quote def)) out)
                0))
         (vol (when (and out (string-match "/[ \t]*\\([0-9]+\\)%" out (or pos 0)))
                (match-string 1 out))))
    (if vol
        (message "Volumen: %s%%" vol)
      (message "vol-debug: def=%s out=(%s)"
               (or def "-") (string-trim (or out ""))))))


(defun clm:volume-change (sign)
  "Interno: aplica SIGN (1 o -1) con el paso que dicta Shift."
  (let* ((with-shift (memq 'shift (event-modifiers last-command-event)))
         (delta (* sign (if with-shift clm:volume-step-shift clm:volume-step))))
    (clm:adjust-volume delta)))

(defun clm:volume-up ()
  "Sube el volumen del sistema; con Shift, paso mayor."
  (interactive)
  (clm:volume-change 1))

(defun clm:volume-down ()
  "Baja el volumen del sistema; con Shift, paso mayor."
  (interactive)
  (clm:volume-change -1))



;; ------------------------------------------------------------------
;; Modo zen: s-<backspace> conmuta vista normal <-> zen.
;; Zen = sin numeros de linea, sin mode-line, sin barras,
;; fringe al minimo y el eco (minibuffer) en blanco.
;; ------------------------------------------------------------------

(defvar clm:zen-active nil
  "Non-nil cuando el modo zen esta activo.")

(defvar clm:zen-saved nil
  "Estado previo de la interfaz, para restaurar al salir del zen.")

(defun clm:zen-on ()
  (setq clm:zen-saved
        (list (default-value 'mode-line-format)
              (bound-and-true-p display-line-numbers-mode)
              (bound-and-true-p linum-mode)
              (bound-and-true-p menu-bar-mode)
              (bound-and-true-p tool-bar-mode)
              (bound-and-true-p scroll-bar-mode)))
  (setq-default mode-line-format nil)
  (when (bound-and-true-p display-line-numbers-mode) (display-line-numbers-mode -1))
  (when (bound-and-true-p linum-mode)               (linum-mode -1))
  (menu-bar-mode -1)
  (tool-bar-mode -1)
  (scroll-bar-mode -1)
  (set-fringe-style 0)
  (setq clm:zen-active t)
  (force-mode-line-update)
  (message nil))

(defun clm:zen-off ()
  (let ((s clm:zen-saved))
    (setq-default mode-line-format (nth 0 s))
    (when (nth 1 s) (display-line-numbers-mode 1))
    (when (nth 2 s) (linum-mode 1))
    (menu-bar-mode   (if (nth 3 s) 1 -1))
    (tool-bar-mode   (if (nth 4 s) 1 -1))
    (scroll-bar-mode (if (nth 5 s) 1 -1))
    (set-fringe-style nil)
    (setq clm:zen-active nil)
    (force-mode-line-update))
  (message "Modo zen desactivado"))

(defun clm:toggle-zen ()
  "Conmuta entre la visualizacion normal y el modo zen."
  (interactive)
  (if clm:zen-active (clm:zen-off) (clm:zen-on)))


;;;; Asignación de teclas

(global-set-key (kbd "M-+") #'clm:increment-number-at-point)
(global-set-key (kbd "M--") #'clm:decrement-number-at-point)
(global-set-key (kbd "s-<escape>") 'save-buffer)
(global-set-key (kbd "s-e") #'clm:send-block-to-file)

(global-set-key (kbd "s-<kp-add>")        #'clm:volume-up)
(global-set-key (kbd "s-S-<kp-add>")      #'clm:volume-up)
(global-set-key (kbd "s-<kp-subtract>")   #'clm:volume-down)
(global-set-key (kbd "s-S-<kp-subtract>") #'clm:volume-down)

(global-set-key (kbd "s-<backspace>") #'clm:toggle-zen)
