class _Any:
    def __init__(self,*a,**k): pass
    def __getattr__(self,n): return _Any()
    def __call__(self,*a,**k): return _Any()
Frame=Tk=Canvas=Label=Button=PhotoImage=Toplevel=_Any
def __getattr__(n): return _Any
