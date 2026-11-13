def __getattr__(n):
    from tkinter import _Any; return _Any
