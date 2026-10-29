;;; pilalcbb-mode.el --- Major mode for Pila-Bytebeat stack language -*- lexical-binding: t; -*-
;; Version: 0.2.1
;; Keywords: languages
;; Package-Requires: ((emacs "24.3"))

;;; Commentary:
;; Major mode for Pila-Livecoding-Bytebeat files (.p).
;; Provides syntax highlighting, comments, strings and indentation.

;;; Code:

(defgroup pilalcbb nil
  "Major mode for Pila-Bytebeat."
  :group 'languages
  :prefix "pilalcbb-")

(defcustom pilalcbb-indent-offset 2
  "Number of spaces for each Pila-Livecoding-Bytebeat indentation level."
  :type 'integer
  :group 'pilalcbb)

(defvar pilalcbb-mode-syntax-table
  (let ((st (make-syntax-table)))
    (modify-syntax-entry ?\s " " st)
    (modify-syntax-entry ?\t " " st)
    (modify-syntax-entry ?\n " " st)
    (modify-syntax-entry ?\f " " st)
    ;; Comments are written as ( ... ).
    (modify-syntax-entry ?\( "<" st)
    (modify-syntax-entry ?\) ">" st)
    ;; Strings use ASCII double quotes.
    ;; $ means paired delimiter: starts and ends a string.
    (modify-syntax-entry ?\" "$" st)
    ;; Blocks and brackets used for indentation and navigation.
    (modify-syntax-entry ?\{ "(}" st)
    (modify-syntax-entry ?\} "){" st)
    (modify-syntax-entry ?\[ "(]" st)
    (modify-syntax-entry ?\] ")[" st)
    ;; Operators and punctuation are symbol constituents.
    ;; This helps font-lock treat words like +, <<, =:, ::, etc. as tokens.
    (mapc (lambda (c) (modify-syntax-entry c "_" st))
          '(?+ ?- ?* ?/ ?% ?= ?< ?> ?& ?| ?^ ?~ ?@ ?# ?: ?. ?? ?! ?_ ?\;))
    st)
  "Syntax table for `pilalcbb-mode'.")

(defconst pilalcbb-control-words
  '("SR" "CH" "BUF"
    "VMIN" "VMAX" "VINT"
    "SCOPE" "SZOOM" "SCOLOR" "SSTROKE" "STYPE" "SFFT" "SRES"
    "SWINX" "SWINY" "SWINW" "SWINH" "SFULLSCR" "SDECOR" "SBGCOLOR" "SWINTOP"
    "DELAY" "DTIME" "DFEED" "DMIX" "DFHI" "DFLO"
    "SMPFADE"
    "DCB" "SAT" "LP" "MUTE" "STRICT" "BCASC"
    "OUT" "T" "bye" "anomlog" "default" "reload" "showlimits"
    )
  "Audio configuration setters and application control words.")

(defconst pilalcbb-getter-words
  '("sr" "ch" "vmin" "vmax" "buf" "t" "i" "j" "scrw" "scrh" "winx" "winy" "winw" "winh" "bcasc")
  "Audio getter and context variable words.")

(defconst pilalcbb-oscillator-words
  '("sine" "triangle" "noise" "saw" "sawpb" "square" "squarepb")
  "Oscillator generator words.")

(defconst pilalcbb-recording-words
  '("recfile" "recstart" "recstop")
  "Recording control words.")

(defconst pilalcbb-samples-words
  '("smpload" "smplive" "sample" "smplen" "smpremove" "smpreset"
    "smploop" "smpreverse" "smpgain" "smpmix" "samplepitch" )
  "Samples control words.")

(defconst pilalcbb-stack-words
  '("e" "d" "nd" "r" "nr" "cs" "ls")
  "Stack manipulation words.")

(defconst pilalcbb-math-words
  '(
    "gsin*" "gcos*" "gtan*"
    "rsin*" "rcos*" "rtan*"
    "hsin*" "hcos*" "htan*"
    "logn*" "exp*" "sqrt*"
    "gsin" "gcos" "gtan" "rsin"
    "rcos" "rtan" "hsin" "hcos"
    "htan" "logn" "exp" "sqrt"
    "abs" "min" "max"
    )
  "Math function words.")

(defconst pilalcbb-operator-words
  '("<<" "<=" ">>" ">=" "==" "!="
    "+" "-" "*" "/" "%" "<" ">"
    "&" "|" "^" "~")
  "Arithmetic, comparison and binary operators.")

(defconst pilalcbb-io-words
  '("=:" ":=" ".:" "_:" "::")
  "Alias, printing and stack inspection words.")

(defconst pilalcbb-special-words
  '("..." "@!" "?!" "?" ",_" )
  "Special control tokens not directly handled by bracket syntax.")

(defun pilalcbb--token-regexp (words)
  "Return a font-lock regexp matching WORDS as whole symbols."
  (let ((sorted (sort (copy-sequence words)
                      (lambda (a b) (> (length a) (length b))))))
    (concat "\\(?:^\\|\\s-\\)\\("
            (mapconcat #'regexp-quote sorted "\\|")
            "\\)\\_>")))

(defconst pilalcbb-font-lock-keywords
  `(
    ;; Context blocks: {a {b {s {a.} {b.} and closing }.
    ("{[abs]\\(?:\\.\\)?\\|}" . font-lock-keyword-face)
    ;; Word definitions: : name ... ;
    ("\\(?:^\\|\\s-\\)\\(:\\)\\_>\\s-+\\([^[:space:]]+\\)"
     (1 font-lock-keyword-face)
     (2 font-lock-function-name-face))
    ("\\(?:^\\|\\s-\\)\\(;\\)\\_>" 1 font-lock-keyword-face)
    ;; Jumps and labels.
    ("\\(?:^\\|\\s-\\)\\(->\\)\\([A-Za-z0-9_-]+\\)\\_>"
     (1 font-lock-keyword-face)
     (2 font-lock-constant-face))
    ("\\(?:^\\|\\s-\\)\\(#\\)\\([A-Za-z0-9_-]+\\)\\_>"
     (1 font-lock-preprocessor-face)
     (2 font-lock-constant-face))
    ;; Conditional and loop fences with brackets.
    ("\\(?:\\?\\[\\|@\\[\\|\\]@\\|\\]\\?\\)" . font-lock-keyword-face)
    ;; Integer literals with explicit base: <digits>_<max-digit>
    ;; Examples: 1010110'1 (binary), ff00b1'f (hex), DEADBEEF'Z (base 62)
    ("\\(?:^\\|\\s-\\)\\(-?[0-9a-zA-Z]+'[0-9a-zA-Z]\\)\\_>" 1 font-lock-constant-face)
    ;; Plain decimal integer literals
    ("\\(?:^\\|\\s-\\)\\(-?[0-9]+\\)\\_>" 1 font-lock-constant-face)
    ;; Token classes.
    (,(pilalcbb--token-regexp pilalcbb-special-words) 1 font-lock-keyword-face)
    (,(pilalcbb--token-regexp pilalcbb-io-words) 1 font-lock-keyword-face)
    (,(pilalcbb--token-regexp pilalcbb-operator-words) 1 font-lock-builtin-face)
    (,(pilalcbb--token-regexp pilalcbb-control-words) 1 font-lock-type-face)
    (,(pilalcbb--token-regexp pilalcbb-getter-words) 1 font-lock-variable-name-face)
    (,(pilalcbb--token-regexp pilalcbb-oscillator-words) 1 font-lock-function-name-face)
    (,(pilalcbb--token-regexp pilalcbb-recording-words) 1 font-lock-builtin-face)
    (,(pilalcbb--token-regexp pilalcbb-samples-words) 1 font-lock-builtin-face)
    (,(pilalcbb--token-regexp pilalcbb-stack-words) 1 font-lock-keyword-face)
    (,(pilalcbb--token-regexp pilalcbb-math-words) 1 font-lock-function-name-face))
  "Highlighting rules for Pila-Livecoding-Bytebeat mode.")

(defun pilalcbb--count-line-closers ()
  "Count block closers at the beginning of the current line."
  (save-excursion
    (beginning-of-line)
    (skip-chars-forward " \t")
    (let ((count 0))
      (while (looking-at "\\(?:}\\|\\]@\\|\\]\\?\\)")
        (setq count (1+ count))
        (goto-char (match-end 0))
        (skip-chars-forward " \t"))
      count)))

(defun pilalcbb--state-at (pos)
  "Scanner state at POS: 'normal, 'string, 'comment, 'off or 'offstr.
Same rules as the C parser, independent of syntax-ppss."
  (save-excursion
    (goto-char (point-min))
    (let ((state 'normal)
          (depth 0))
      (while (and (< (point) pos)
                  (re-search-forward "[\"(){}]" pos t))
        (let ((c (char-before)))
          (cond
           ((eq state 'string)
            (when (eq c ?\") (setq state 'normal)))
           ((eq state 'comment)
            (cond
             ((eq c ?\() (setq depth (1+ depth)))
             ((eq c ?\))
              (setq depth (1- depth))
              (when (= depth 0) (setq state 'normal)))))
           ((eq state 'off)
            (cond
             ((eq c ?\") (setq state 'offstr))
             ((eq c ?\}) (setq state 'normal))))
           ((eq state 'offstr)
            (when (eq c ?\") (setq state 'off)))
           ((eq c ?\") (setq state 'string))
           ((eq c ?\() (setq state 'comment depth 1))
           ((eq c ?\{)
            (let ((marker
                   (buffer-substring-no-properties
                    (point)
                    (save-excursion (skip-chars-forward "^ \t\n}") (point)))))
              (unless (member marker '("a" "b" "s" "a." "b."))
                (setq state 'off)))))))
      state)))

(defun pilalcbb--span (beg end skiplines)
  "Return (OPEN . CLOSE) if [beg,end] holds exactly one `( ... )' comment:
opener at first non-blank, closer at last non-blank, balanced nesting.
SKIPLINES non-nil allows leading/trailing newlines (region case)."
  (save-excursion
    (goto-char beg)
    (skip-chars-forward (if skiplines " \t\n" " \t"))
    (let ((open (point))
          (depth 0)
          (close nil))
      (when (and (< open end)
                 (eq (char-after) ?\()
                 (eq (pilalcbb--state-at open) 'normal))
        (while (and (< (point) end) (not close))
          (let ((c (char-after)))
            (cond
             ((eq c ?\() (setq depth (1+ depth)))
             ((eq c ?\))
              (setq depth (1- depth))
              (when (= depth 0) (setq close (point)))))
            (forward-char 1)))
        (when close
          (goto-char end)
          (skip-chars-backward (if skiplines " \t\n" " \t"))
          (when (= (1+ close) (point))
            (cons open close)))))))

(defun pilalcbb-comment-line-or-region (&optional arg)
  "Comment or uncomment current line(s) or region, symmetrically.
Uncomment removes both delimiters of lines that are exactly one comment;
commenting wraps each non-blank, non-comment line.  Never calls
comment-region, so inner parens are never escaped."
  (interactive "*P")
  (let* ((use-region (use-region-p))
         (beg (if use-region (region-beginning) (line-beginning-position)))
         (end (if use-region (region-end) (line-end-position))))
    (save-excursion
      (goto-char beg)
      (beginning-of-line)
      (setq beg (point))
      (goto-char end)
      (if (and (bolp) (/= beg end))
          (setq end (point))
        (end-of-line)
        (setq end (point))))
    (let ((span (pilalcbb--span beg end t)))
      (if span
          (progn
            (delete-region (cdr span) (1+ (cdr span)))
            (delete-region (car span) (1+ (car span))))
        (let ((spans nil)
              (lines 0))
          (save-excursion
            (goto-char beg)
            (while (< (point) end)
              (unless (save-excursion (skip-chars-forward " \t") (eolp))
                (setq lines (1+ lines))
                (let ((s (pilalcbb--span (point) (line-end-position) nil)))
                  (when s (push s spans))))
              (forward-line 1)))
          (if (and spans (= (length spans) lines))
              (dolist (s spans)
                (delete-region (cdr s) (1+ (cdr s)))
                (delete-region (car s) (1+ (car s))))
            (save-excursion
              (goto-char beg)
              (while (< (point) end)
                (unless (or (pilalcbb--span (point) (line-end-position) nil)
                            (save-excursion (skip-chars-forward " \t") (eolp)))
                  (let ((at (progn (skip-chars-forward " \t") (point))))
                    (end-of-line)
                    (skip-chars-backward " \t")
                    (insert ")")
                    (goto-char at)
                    (insert "(")))
                (forward-line 1)))))))))

(defun pilalcbb-comment-dwim (&optional arg)
  "comment-dwim, but region/line uncomment goes through our symmetric
toggle; with no region and no whole-line comment, behaves as comment-dwim."
  (interactive "*P")
  (if (or (use-region-p)
          (pilalcbb--span (line-beginning-position) (line-end-position) nil))
      (pilalcbb-comment-line-or-region arg)
    (comment-dwim arg)))

(defun pilalcbb--def-depth-at (pos)
  "Return how many unmatched `:' definition openers are in effect at POS.
Only lone `:' / `;' tokens count (whitespace-delimited, like the
language itself).  Occurrences inside strings or comments are ignored
via `syntax-ppss', which already knows the mode syntax table."
  (save-excursion
    (goto-char (point-min))
    (let ((depth 0))
      (while (re-search-forward "\\(?:^\\|\\s-\\)\\([:;]\\)\\(?:\\s-\\|$\\)" pos t)
        (let* ((at (match-beginning 1))
               (state (syntax-ppss at)))
          (unless (or (nth 3 state) (nth 4 state))
            (if (eq (char-after at) ?\:)
                (setq depth (1+ depth))
              (setq depth (max 0 (1- depth)))))
          (goto-char (1+ at))))
      depth)))

(defun pilalcbb--line-starts-with-def-closer ()
  "Non-nil if the current line starts with a lone `;' token."
  (save-excursion
    (beginning-of-line)
    (skip-chars-forward " \t")
    (looking-at ";\\(\\s-\\|$\\)")))

(defun pilalcbb-indent-line ()
  "Indent the current Pila-Livecoding-Bytebeat line.
Base indent comes from bracket depth ({ [ and their closers); lines
inside a function definition (`:' ... `;') get one extra level per
unmatched opener, and a lone `;' line returns to the definition level."
  (interactive)
  (let* ((bol (line-beginning-position))
         (state (syntax-ppss bol))
         (in-string (nth 3 state))
         (in-comment (nth 4 state))
         (col (current-column)))
    (if (or in-string in-comment)
        (when (< col (current-indentation))
          (back-to-indentation))
      (let* ((depth (max 0 (car state)))
             (closers (pilalcbb--count-line-closers))
             (def-depth (pilalcbb--def-depth-at bol))
             (extra (if (pilalcbb--line-starts-with-def-closer)
                        (max 0 (1- def-depth))
                      def-depth))
             (target (max 0 (* (+ (max 0 (- depth closers)) extra)
                               pilalcbb-indent-offset))))
        (indent-line-to target)
        (if (< col (current-indentation))
            (back-to-indentation)
          (move-to-column col))))))


(defun pilalcbb--extend-region (start end)
  "Widen the propertize region to the start of the containing string
or comment, so the state machine always begins at a boundary."
  (let ((state (syntax-ppss start)))
    (when (or (nth 3 state) (nth 4 state))
      (let ((s (nth 8 state)))
        (when (and s (< s start))
          (cons s end))))))

(defun pilalcbb--syntax-propertize (start end)
  "Rescan from point-min to END applying syntactic overrides.
Starting from a known-clean origin removes the need for `syntax-ppss'
here: calling it from inside the propertize machinery reenters the
engine and blows `max-specpdl-size'.  The scan replicates the C parser:
nested `( ... )' comments, inert quotes inside comments, inert parens
inside strings, and `{x ... }' off-blocks as comment regions."
  (goto-char (point-min))
  (let ((state 'normal)   ; normal | string | comment | off | offstr
        (depth 0))
    (while (< (point) end)
      (let ((c (char-after)))
        (cond
         ((eq state 'string)
          (when (eq c ?\") (setq state 'normal))
          (forward-char 1))
         ((eq state 'comment)
          (cond
           ((eq c ?\()
            (put-text-property (point) (1+ (point)) 'syntax-table
                               (string-to-syntax "."))
            (setq depth (1+ depth))
            (forward-char 1))
           ((eq c ?\))
            (setq depth (1- depth))
            (when (> depth 0)
              (put-text-property (point) (1+ (point)) 'syntax-table
                                 (string-to-syntax ".")))
            (when (= depth 0) (setq state 'normal))
            (forward-char 1))
           (t (forward-char 1))))

         ((eq state 'off)
          (cond
           ((eq c ?\")
            (setq state 'offstr)
            (forward-char 1))
           ((eq c ?\})
            (put-text-property (point) (1+ (point)) 'syntax-table
                               (string-to-syntax ">"))
            (setq state 'normal)
            (forward-char 1))
           ((or (eq c ?\() (eq c ?\)))
            (put-text-property (point) (1+ (point)) 'syntax-table
                               (string-to-syntax "."))
            (forward-char 1))
           (t (forward-char 1))))
         ((eq state 'offstr)
          (cond
           ((eq c ?\")
            (setq state 'off)
            (forward-char 1))
           ((or (eq c ?\() (eq c ?\)))
            (put-text-property (point) (1+ (point)) 'syntax-table
                               (string-to-syntax "."))
            (forward-char 1))
           (t (forward-char 1))))

;         ((eq state 'off)
;          (cond
;           ((eq c ?\")
;            (setq state 'offstr)
;            (forward-char 1))
;
;           ;;; --- Bloque indefinido - cierre
;           ((eq c ?\})
;            (put-text-property (point) (1+ (point)) 'syntax-table
;                               (string-to-syntax ">"))
;            (setq state 'normal)
;            (forward-char 1))
;
;           (t (forward-char 1))))
;         ((eq state 'offstr)
;          (when (eq c ?\") (setq state 'off))
;          (forward-char 1))
         (t ; normal
          (cond
           ((eq c ?\")
            (setq state 'string)
            (forward-char 1))
           ((eq c ?\()
            (setq state 'comment depth 1)
            (forward-char 1))

           ;;; --- Bloque indefinido - apertura
           ;;; Resaltado a modo de comentario
           ((eq c ?\{)
            (let* ((ms (1+ (point)))
                   (me (save-excursion
                         (goto-char ms)
                         (skip-chars-forward "^ \t\n}")
                         (point)))
                   (marker (buffer-substring-no-properties ms me)))
              (if (member marker '("a" "b" "s" "a." "b."))
                  (forward-char 1)
                (put-text-property (point) (1+ (point)) 'syntax-table
                                   (string-to-syntax "<"))
                (setq state 'off)
                (forward-char 1))))

           (t (forward-char 1)))))))))

(defvar pilalcbb-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map (kbd "C-c C-c") #'pilalcbb-comment-dwim)
    (define-key map (kbd "C-x C-;") #'pilalcbb-comment-line-or-region)
    map)
  "Keymap for `pilalcbb-mode'.")

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.p\\'" . pilalcbb-mode))

;;;###autoload
(define-derived-mode pilalcbb-mode prog-mode "Pila-Livecoding-Bytebeat"
  "Major mode for editing Pila-Livecoding-Bytebeat files."
  (set-syntax-table pilalcbb-mode-syntax-table)

  (setq-local comment-start "(")
  (setq-local comment-end ")")
  (setq-local comment-start-skip "(\\s-*")
  (setq-local comment-end-skip "\\s-*")
  (setq-local comment-multi-line t)
  (setq-local indent-line-function #'pilalcbb-indent-line)
  (setq-local font-lock-syntactic-face-function
              (lambda (state)
                (if (nth 3 state)
                    font-lock-string-face
                  font-lock-comment-face)))
  (setq-local font-lock-defaults '(pilalcbb-font-lock-keywords nil nil))
  (setq-local electric-indent-inhibit t)
  (setq-local syntax-propertize-function #'pilalcbb--syntax-propertize)
  )


(provide 'pilalcbb-mode)
;;; pilalcbb-mode.el ends here
