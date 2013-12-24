/*
Copyright (c) 2013 Yamada Yohei <yamadayohei@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
#include <windows.h>
#define CLASSNAME_TOPLEVEL TEXT("lsbox")
#define CLASSNAME_ISEARCH  TEXT("isearch")
#define CLASSNAME_PREVIEW  TEXT("preview")
#define GREY(x) RGB(x, x, x)
#define Ctl_(x) (x - 'a' + 1)

#define LSTSIZE 100
#define FILENAME_MASK_BUFSIZE 32
#define MYWM_ISEARCH      WM_USER + 0
#define MYWM_ISEARCH_NEXT WM_USER + 1

const COLORREF RULER_COLOUR = GREY(0x70);
const COLORREF  TEXT_COLOUR = GREY(0xa0);
const COLORREF    BG_COLOUR = GREY(0x30);
const COLORREF  MARK_COLOUR = RGB(0xa0, 0x80, 0x40);

const int RPANE_X = 0x200;

struct myls {
   TCHAR nam[256];
   TCHAR lst[80];
   DWORD siz;
   BYTE  typ;
};
enum {
   MYLS_FILE,
   MYLS_DIR,
};

struct myls  myls_a[LSTSIZE];
struct myls  myls_b[LSTSIZE];
struct myls *myls   = myls_a;
struct myls *myls_r = myls_b;

BYTE  mark[LSTSIZE];
BYTE  nlst;
TCHAR filename_mask[FILENAME_MASK_BUFSIZE] = TEXT("*");
TCHAR preview_text[4096] = TEXT("Preview Window");
TCHAR Ruler[] = TEXT("0.......8......10......18......20......28......30"
                      "......38......40......48......50......58......60");
TCHAR other_dir[256];
LPCTSTR preview_text_lines[80] = { preview_text, NULL };
WNDPROC edit_orig_wproc;
BOOL preview_visible;
int arg_n, arg_m;

BOOL
update_preview_text(LPTSTR text, LPCTSTR *lines, HANDLE f)
{
   BYTE buf[16];
   DWORD n_read;
   BOOL result;
   int j, n = 0;
   LPTSTR l, p;

   l = p = text;
   for (;;) {
      result = ReadFile(f, buf, sizeof buf, &n_read, NULL);
      if (!result) break;
      if (!n_read) break;
      for (j = 0; j < n_read; j++) {
         if (p + 1 == &preview_text[4096]) {
            *p++ = '\0';
            lines[n++] = l;
            goto out;
         }
         if (!buf[j]) {
            /* This file is not plain text */
            SetFilePointer(f, 0, NULL, FILE_BEGIN);
            lines[0] = NULL;
            return FALSE;
         }
         if (buf[j] == '\n') {
            *p++ = '\0';
            lines[n++] = l;
            l = p;
            if (n == 80 - 2) goto out;
         } else if (p >= &l[80]) {
         } else {
            *p++ = buf[j];
         }
      }
   }
out:
   lines[n] = NULL;
   return TRUE;
}

BOOL
update_preview_dump(LPTSTR buf2, HANDLE f)
{
   BYTE buf[16];
   DWORD n_read;
   LPCTSTR fmt;
   BOOL result;
   int len = 0, len2, i, j;
   LPTSTR l;

   for (i = 1; i < 80 - 1; i++) {
      result = ReadFile(f, buf, sizeof buf, &n_read, NULL);
      if (!result) break;
      if (!n_read) break;
      l = &buf2[len];
      len2 = 0;
      for (j = 0; j < n_read; j++) {
         fmt = (buf[j] >= ' ' && buf[j] <= '~') ? TEXT("  %c")
                                                : TEXT(" %02x");
         len2 += wsprintf(&l[len2], fmt, buf[j]);
      }
      preview_text_lines[i] = l;
      len += len2 + 1;
   }
   preview_text_lines[i] = NULL;
   return TRUE;
}

void
update_preview(int p)
{
   HANDLE f;
   BOOL result;
   int len;

   lstrcpy(preview_text, myls[p].nam);
   len = lstrlen(preview_text) + 1;

   f = CreateFile(myls[p].nam, GENERIC_READ, FILE_SHARE_READ, NULL,
                                            OPEN_EXISTING, 0, NULL);
   if (f == INVALID_HANDLE_VALUE) {
      preview_text_lines[1] = NULL;
      return;
   }

   result = update_preview_text(&preview_text[len], &preview_text_lines[1], f);

   if (result == FALSE)
      update_preview_dump(&preview_text[len], f);

   CloseHandle(f);
}

void
swap_pane(void)
{
   struct myls *myls_t;

   myls_t = myls;
   myls   = myls_r;
   myls_r = myls_t;
}

void
swap_pane_user(void)
{
   TCHAR tmp_dir[256];

   lstrcpy(tmp_dir, other_dir);
   GetCurrentDirectory(256, other_dir);
   SetCurrentDirectory(tmp_dir);
   swap_pane();
}

/* my strstr implementation */
BOOL
mystrstr_sub(LPCTSTR hay, LPCTSTR needle)
{
   LPCTSTR p, q;

   p = hay;
   for (q = needle; *q; q++)
      if (*p++ != *q)
         return FALSE;

   return TRUE;
}

LPCTSTR
mystrstr(LPCTSTR haystack, LPCTSTR needle)
{
   LPCTSTR p;

   for (p = haystack; *p; p++)
      if (mystrstr_sub(p, needle))
         return p;

   return NULL;
}

void
touch_file(LPCTSTR filename)
{
   HANDLE f;

   f = CreateFile(filename, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
   CloseHandle(f);
}

void
spawn_notepad(LPCTSTR filename)
{
   STARTUPINFO si;
   PROCESS_INFORMATION pi;
   TCHAR buf[256];

   memset(&si, 0, sizeof si);
   si.cb = sizeof si;
   lstrcpy(buf, TEXT("notepad.exe "));
   lstrcat(buf, filename);

   CreateProcess(NULL, buf, NULL,
         NULL, FALSE, 0, NULL, NULL, &si, &pi);
}

void
mark_all(void)
{
   int i;

   for (i = 0; i < nlst; i++)
      mark[i] |= 1;
}

void
clear_marks(void)
{
   int i;

   for (i = 0; i < LSTSIZE; i++)
      mark[i] = 0;
}

#define decode_one_attr(out, attr, ch, attrbit) \
   do { out = (attr & FILE_ATTRIBUTE_ ## attrbit) ? ch : '.'; } while (0)

void
decode_attr(PTSTR buf, DWORD attr)
{
   decode_one_attr(buf[0], attr, 'a', ARCHIVE);
   decode_one_attr(buf[1], attr, 'd', DIRECTORY);
   decode_one_attr(buf[2], attr, 's', SYSTEM);
   decode_one_attr(buf[3], attr, 'h', HIDDEN);
   decode_one_attr(buf[4], attr, 'r', READONLY);
   buf[5] = '\0';
}

void
size_unit(PTSTR buf, DWORD size)
{
   TCHAR unit[] = TEXT("BKM"), *p, *q;

   for (p = unit; *p; p++) {
      if (size < 10 * 0x400) break;
      size /= 0x400;
   }
   if (size >= 0x400) p++;
   if (!*p) {
      lstrcpy(buf, TEXT(" HUGE"));
      return;
   }
   q = &buf[5];
   *q-- = '\0';
   *q-- = *p;
   if (size >= 0x400) {
      buf[0] = ' ';
      buf[1] = '0' + size / 0x400;
      buf[2] = '.';
      buf[3] = '0' + size % 0x400 / 103;
      return;
   }
   if (!size) *q-- = '0';
   for (; size; size /= 10) {
      *q-- = '0' + size % 10;
      if (q < buf) break;
   }
   for (; q >= buf; q--) {
      *q = ' ';
   }
}

void
fmt_time(PTSTR buf, FILETIME ft)
{
   SYSTEMTIME st, now;
   WORD y, mi, d, h, m;
   PTSTR mnm[] = { NULL,
         TEXT("Jan"), TEXT("Feb"), TEXT("Mar"), TEXT("Apr"),
         TEXT("May"), TEXT("Jun"), TEXT("Jul"), TEXT("Aug"),
         TEXT("Sep"), TEXT("Oct"), TEXT("Nov"), TEXT("Dec"),
   };

   FileTimeToSystemTime(&ft, &st);
   y = st.wYear % 100;
   mi = st.wMonth;
   d = st.wDay;
   h = st.wHour;
   m = st.wMinute;
   // Long format
   //wsprintf(buf, TEXT("%02d-%02d-%02d %02d:%02d"), y, mi, d, h, m);
   //wsprintf(buf, TEXT("%04d.%02d%02d.%02d%02d"), st.wYear, mi, d, h, m);

   // Short format
   GetLocalTime(&now);
   if (st.wYear == now.wYear)
      wsprintf(buf, TEXT("%02d-%02d %02d:%02d"), mi, d, h, m);
   else
      wsprintf(buf, TEXT("%02d-%02d  %04d"), mi, d, st.wYear);

   if (st.wYear == now.wYear)
      wsprintf(buf, TEXT("%2d.%s %2d:%02d"), d, mnm[mi], h, m);
   else
      wsprintf(buf, TEXT("%2d.%s  %04d"), d, mnm[mi], st.wYear);
}

void
mklst(int skip)
{
   HANDLE dir;
   WIN32_FIND_DATA find;
   int i, len;
   TCHAR attr_buf[6], size_buf[6], date_buf[15];

   for (i = 0; i < LSTSIZE; i++)
      lstrcpy(myls[i].lst, TEXT("~"));

   dir = FindFirstFile(filename_mask, &find);
   for (i = 0; i < skip; i++)
      if (!FindNextFile(dir, &find)) {
         FindClose(dir);
         nlst = 0;
         return;
      }

   for (i = 0; i < LSTSIZE; i++) {
      decode_attr(attr_buf, find.dwFileAttributes);
      size_unit(size_buf, find.nFileSizeLow);
      fmt_time(date_buf, find.ftLastWriteTime);

      len = wsprintf(myls[i].lst, TEXT("%s %s %s  "), attr_buf, size_buf, date_buf);
      len = wsprintf(myls[i].lst + len, TEXT("%12s  "),
            find.cAlternateFileName[0] ? find.cAlternateFileName
                                       : find.cFileName);
      lstrcat(myls[i].lst,      find.cFileName);
      lstrcpy(myls[i].nam, find.cFileName);
      myls[i].siz = find.nFileSizeLow;
      myls[i].typ = (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ?
            MYLS_DIR : MYLS_FILE;

      if (!FindNextFile(dir, &find)) break;
   }
   nlst = i + 1;
   FindClose(dir);
}

LRESULT CALLBACK
preview_wproc(HWND w, UINT mesg, WPARAM wp, LPARAM lp)
{
   HDC          dc;
   PAINTSTRUCT  ps;
   LPCTSTR *p = preview_text_lines;
   int i;

   switch (mesg) {
   case WM_PAINT:
      dc = BeginPaint(w, &ps);
      SelectObject(dc, GetStockObject(ANSI_FIXED_FONT));
      SetTextColor(dc, TEXT_COLOUR);
      SetBkColor(dc, BG_COLOUR);
      for (i = 0; p[i]; i++)
         TextOut(dc, 0, i * 0x10, p[i], lstrlen(p[i]));
      EndPaint(w, &ps);
      return 0;
   }
   return DefWindowProc(w, mesg, wp, lp);
}

LRESULT CALLBACK
isearch_wproc(HWND w, UINT mesg, WPARAM wp, LPARAM lp)
{
   HDC          dc;
   PAINTSTRUCT  ps;
   TCHAR buf[256];
   int l;

   switch (mesg) {
   case WM_PAINT:
      dc = BeginPaint(w, &ps);
      SelectObject(dc, GetStockObject(ANSI_FIXED_FONT));
      SetTextColor(dc, TEXT_COLOUR);
      SetBkColor(dc, BG_COLOUR);
      GetWindowText(w, buf, 256);
      TextOut(dc, 0x10, 8, buf, lstrlen(buf));
      EndPaint(w, &ps);
      return 0;
   case WM_CHAR:
      switch ((TCHAR)wp) {
      case Ctl_('m'):
      case '/':
         ShowWindow(w, SW_HIDE);
         return 0;
      case Ctl_('s'):
         SendMessage(GetParent(w), MYWM_ISEARCH_NEXT, wp, 0);
         InvalidateRect(w, NULL, TRUE);
         return 0;
      case Ctl_('u'):
         SetWindowText(w, TEXT(""));
         InvalidateRect(w, NULL, TRUE);
         return 0;
      case ' ' ... '/'-1:
      case '/'+1 ... '~':
         GetWindowText(w, buf, 256);
         l = lstrlen(buf);
         buf[l] = (TCHAR)wp;
         buf[l + 1] = '\0';
         SetWindowText(w, buf);
         SendMessage(GetParent(w), MYWM_ISEARCH, wp, 0);
         InvalidateRect(w, NULL, TRUE);
         return 0;
      }
   }
   return DefWindowProc(w, mesg, wp, lp);
}

LRESULT CALLBACK
edit_my_wproc(HWND w, UINT mesg, WPARAM wp, LPARAM lp)
{
   switch (mesg) {
   case WM_CHAR:
      switch ((TCHAR)wp) {
      case Ctl_('m'):
         ShowWindow(w, SW_HIDE);
         return 0;
      case Ctl_('u'):
         SetWindowText(w, TEXT(""));
         return 0;
      }
   }
   return CallWindowProc(edit_orig_wproc, w, mesg, wp, lp);
}

LRESULT CALLBACK
toplevel_wproc(HWND w, UINT mesg, WPARAM wp, LPARAM lp)
{
   HDC          dc;
   PAINTSTRUCT  ps;
   int i;
   static int p = 0, n_marks = 0;
   static HWND w_isearch, w_preview, w_edit;
   TCHAR editbuf[256];

   switch (mesg) {
   case WM_CREATE:
      w_isearch = CreateWindow(CLASSNAME_ISEARCH, TEXT(""), WS_CHILD,
            0,0, 240,32, w, (HMENU)2, ((LPCREATESTRUCT)lp)->hInstance, NULL);

      w_preview = CreateWindow(CLASSNAME_PREVIEW, TEXT(""), WS_CHILD,
            RPANE_X,0, 480,640, w, 0, ((LPCREATESTRUCT)lp)->hInstance, NULL);
      preview_visible = FALSE;

      w_edit = CreateWindow(TEXT("EDIT"), TEXT(""), WS_CHILD,
            0,0, 240,32, w, (HMENU)1, ((LPCREATESTRUCT)lp)->hInstance, NULL);
      edit_orig_wproc = (WNDPROC)GetWindowLong(w_edit, GWL_WNDPROC);
      SetWindowLong(w_edit, GWL_WNDPROC, (LONG)edit_my_wproc);
      GetCurrentDirectory(256, other_dir);
      mklst(0);
      swap_pane();
      mklst(0);
      return 0;
   case WM_PAINT:
      dc = BeginPaint(w, &ps);
      SelectObject(dc, GetStockObject(ANSI_FIXED_FONT));
      SetTextColor(dc, RULER_COLOUR);
      SetBkColor(dc, BG_COLOUR);
      TextOut(dc, 0x20, 0, Ruler, lstrlen(Ruler));

      SetTextColor(dc, TEXT_COLOUR);
      for (i = 0; i < LSTSIZE; i++) {
         {
            LPCTSTR c;

            if (mark[i] & 1) {
               c = TEXT("*");
            } else if ((n_marks < 1) && i == p) {
               c = TEXT(">");
            } else {
               c = TEXT(" ");
            }
            SetTextColor(dc, MARK_COLOUR);
            TextOut(dc, 0x10, (i + 1) * 0x10, c, 1);
            SetTextColor(dc, TEXT_COLOUR);
         }
         if (i == p) {
            SetTextColor(dc, BG_COLOUR);
            SetBkColor(dc, TEXT_COLOUR);
         }
         TextOut(dc,  0x20, (i + 1) * 0x10, myls[i].lst, lstrlen(myls[i].lst));
         if (i == p) {
            SetTextColor(dc, TEXT_COLOUR);
            SetBkColor(dc, BG_COLOUR);
         }
      }
      swap_pane();
      for (i = 0; i < LSTSIZE; i++)
         TextOut(dc, RPANE_X, (i + 1) * 0x10, myls[i].lst, lstrlen(myls[i].lst));
      swap_pane();

      EndPaint(w, &ps);
      return 0;
   case WM_CHAR:
      switch ((TCHAR)wp) {
      case Ctl_('m'):
         n_marks += (mark[p] & 1) ? -1 : 1;
         mark[p] ^= 1;
         InvalidateRect(w, NULL, FALSE);
         return 0;
      case Ctl_('i'):
         swap_pane_user();
         goto revert;
      case '+':
         GetWindowText(w_edit, editbuf, 256);
         CreateDirectory(editbuf, NULL);
         goto revert;
      case ',':
         arg_m = arg_n;
         arg_n = 0;
         return 0;
      case '/':
         SetWindowText(w_isearch, TEXT(""));
         ShowWindow(w_isearch, SW_SHOW);
         SetFocus(w_isearch);
         return 0;
      case '0' ... '9':
         arg_n = arg_n * 10 + ((TCHAR)wp - '0');
         return 0;
      case '<':
         p = 0;
         InvalidateRect(w, NULL, FALSE);
         break;
      case '>':
         p = nlst - 1;
         InvalidateRect(w, NULL, FALSE);
         break;
      case 'c':
         GetWindowText(w_edit, editbuf, 256);
         if (myls[p].typ == MYLS_FILE)
            CopyFile(myls[p].nam, editbuf, TRUE);
         goto revert;
      case 'e':
         ShowWindow(w_edit, SW_SHOW);
         SetFocus(w_edit);
         return 0;
      case 'E':
         if (myls[p].typ == MYLS_FILE)
            spawn_notepad(myls[p].nam);
         return 0;
      case 'j':
         if (!arg_n) arg_n = 1;
         for (i = 0; i < arg_n; i++) p++;
         arg_n = arg_m = 0;
         InvalidateRect(w, NULL, FALSE);
         break;
      case 'k':
         if (!arg_n) arg_n = 1;
         for (i = 0; i < arg_n; i++) p--;
         arg_n = arg_m = 0;
         InvalidateRect(w, NULL, FALSE);
         break;
      case 'm':
         if (!arg_n) arg_n = 1;
         if (arg_n + p > nlst) arg_n = nlst - p;
         for (i = 0; i < arg_n; i++) {
            n_marks += (mark[p] & 1) ? -1 : 1;
            mark[p++] ^= 1;
         }
         arg_n = arg_m = 0;
         InvalidateRect(w, NULL, FALSE);
         break;
      case 'M':
         if (!arg_n) {
            clear_marks();
            n_marks = 0;
         } else {
            mark_all();
            n_marks = nlst;
         }
         arg_n = arg_m = 0;
         InvalidateRect(w, NULL, FALSE);
         return 0;
      case 'o':
         if (myls[p].typ != MYLS_DIR) return 0;
         SetCurrentDirectory(myls[p].nam);
         goto revert;
      case 'P':
         GetWindowText(w_edit, editbuf, 256);
         if (lstrlen(editbuf) > FILENAME_MASK_BUFSIZE) return 0;
         lstrcpy(filename_mask, editbuf);
         if (!filename_mask[0])
            lstrcpy(filename_mask, TEXT("*"));
         goto revert;
      case 'r':
         mklst(arg_n);
         clear_marks();
         n_marks = 0;
         arg_n = arg_m = 0;
         p = 0;
         InvalidateRect(w, NULL, TRUE);
         break;
      case Ctl_('r'):
      revert:
         mklst(0);
         clear_marks();
         n_marks = 0;
         InvalidateRect(w, NULL, TRUE);
         break;
      case 't':
         GetWindowText(w_edit, editbuf, 256);
         touch_file(editbuf);
         goto revert;
      case 'u':
         SetCurrentDirectory(TEXT(".."));
         goto revert;
      case 'V':
         preview_visible = !preview_visible;
         if (preview_visible) update_preview(p);
         ShowWindow(w_preview, preview_visible? SW_SHOW : SW_HIDE);
         return 0;
      case 'x':
         /* clean empty files and dirs */
         for (i = 0; i < LSTSIZE; i++)
            if (mark[i] & 1) {
               if (myls[i].typ == MYLS_DIR)
                  RemoveDirectory(myls[i].nam);
               if (myls[i].typ == MYLS_FILE && myls[i].siz == 0)
                  DeleteFile(myls[i].nam);
            }
         goto revert;

      /* quit myls */
      case 'q':
         PostQuitMessage(0);
         return 0;
      }

      /* p may have changed */
      if (p < 0) p = 0;
      if (p >= nlst) p = nlst - 1;
      if (preview_visible) update_preview(p);
      InvalidateRect(w_preview, NULL, TRUE);
      return 0;
   case MYWM_ISEARCH:
      GetWindowText(w_isearch, editbuf, 256);
      for (i = p; i < nlst; i++)
         if (mystrstr(myls[i].nam, editbuf)) {
            p = i;
            InvalidateRect(w, NULL, FALSE);
            break; /* exit for */
         }
      if (preview_visible) update_preview(p);
      InvalidateRect(w_preview, NULL, TRUE);
      return 0;
   case MYWM_ISEARCH_NEXT:
      GetWindowText(w_isearch, editbuf, 256);
      for (i = p + 1; i < nlst; i++)
         if (mystrstr(myls[i].nam, editbuf)) {
            p = i;
            InvalidateRect(w, NULL, FALSE);
            break; /* exit for */
         }
      if (preview_visible) update_preview(p);
      InvalidateRect(w_preview, NULL, TRUE);
      return 0;
   case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
   }
   return DefWindowProc(w, mesg, wp, lp);
}

int WINAPI
WinMain(HINSTANCE inst, HINSTANCE pinst, PSTR cmdline, int cmdshow)
{
   WNDCLASS wc = { 0 };
   MSG m;

   /* common */
   wc.style = CS_HREDRAW | CS_VREDRAW;
   wc.hInstance = inst;
   wc.hCursor = LoadCursor(NULL, IDC_ARROW);
   wc.hbrBackground = CreateSolidBrush(BG_COLOUR);

   /* main window */
   wc.lpszClassName = CLASSNAME_TOPLEVEL;
   wc.lpfnWndProc = toplevel_wproc;
   if (!RegisterClass(&wc)) return 1;

   /* isearch window */
   wc.lpszClassName = CLASSNAME_ISEARCH;
   wc.lpfnWndProc = isearch_wproc;
   if (!RegisterClass(&wc)) return 1;

   /* preview window */
   wc.lpszClassName = CLASSNAME_PREVIEW;
   wc.lpfnWndProc = preview_wproc;
   if (!RegisterClass(&wc)) return 1;

   if (!CreateWindow(CLASSNAME_TOPLEVEL, TEXT(__FILE__),
                     WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
                     CW_USEDEFAULT, CW_USEDEFAULT,
                     CW_USEDEFAULT, CW_USEDEFAULT,
                     NULL, NULL, inst, NULL))
      return 1;

   while (GetMessage(&m, NULL, 0, 0) > 0) {
      TranslateMessage(&m);
      DispatchMessage(&m);
   }
   return m.wParam;
}
