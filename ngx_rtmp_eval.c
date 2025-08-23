
/*
 * Copyright (C) Roman Arutyunyan
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_rtmp_eval.h"


#define NGX_RTMP_EVAL_BUFLEN    16


static void
ngx_rtmp_eval_session_str(void *ctx, ngx_rtmp_eval_t *e, ngx_str_t *ret)
{
    *ret = *(ngx_str_t *) ((u_char *) ctx + e->offset);
}


static void
ngx_rtmp_eval_connection_str(void *ctx, ngx_rtmp_eval_t *e, ngx_str_t *ret)
{
    ngx_rtmp_session_t  *s = ctx;

    *ret = *(ngx_str_t *) ((u_char *) s->connection + e->offset);
}


ngx_rtmp_eval_t ngx_rtmp_eval_session[] = {

    { ngx_string("app"),
      ngx_rtmp_eval_session_str,
      offsetof(ngx_rtmp_session_t, app) },

    { ngx_string("flashver"),
      ngx_rtmp_eval_session_str,
      offsetof(ngx_rtmp_session_t, flashver) },

    { ngx_string("swfurl"),
      ngx_rtmp_eval_session_str,
      offsetof(ngx_rtmp_session_t, swf_url) },

    { ngx_string("tcurl"),
      ngx_rtmp_eval_session_str,
      offsetof(ngx_rtmp_session_t, tc_url) },

    { ngx_string("pageurl"),
      ngx_rtmp_eval_session_str,
      offsetof(ngx_rtmp_session_t, page_url) },

    { ngx_string("addr"),
      ngx_rtmp_eval_connection_str,
      offsetof(ngx_connection_t, addr_text) },

    ngx_rtmp_null_eval
};


static void
ngx_rtmp_eval_append(ngx_buf_t *b, void *data, size_t len, ngx_log_t *log)
{
    size_t  buf_len;

    if (b->last + len > b->end) {
        buf_len = 2 * (b->last - b->pos) + len;

        b->start = ngx_alloc(buf_len, log);
        if (b->start == NULL) {
            return;
        }

        b->last = ngx_cpymem(b->start, b->pos, b->last - b->pos);
        b->pos = b->start;
        b->end = b->start + buf_len;
    }

    b->last = ngx_cpymem(b->last, data, len);
}


static void
ngx_rtmp_eval_append_var(void *ctx, ngx_buf_t *b, ngx_rtmp_eval_t **e,
    ngx_str_t *name, ngx_log_t *log)
{
    ngx_str_t           v;
    ngx_rtmp_eval_t    *ee;

    for (; *e; ++e) {
        for (ee = *e; ee->handler; ++ee) {
            if (ee->name.len == name->len &&
                ngx_memcmp(ee->name.data, name->data, name->len) == 0)
            {
                ee->handler(ctx, ee, &v);
                ngx_rtmp_eval_append(b, v.data, v.len, log);
            }
        }
    }
}


ngx_int_t
ngx_rtmp_eval(void *ctx, ngx_str_t *in, ngx_rtmp_eval_t **e, ngx_str_t *out,
    ngx_log_t *log)
{
    u_char      c, *p;
    ngx_str_t   name;
    ngx_buf_t   b;
    ngx_uint_t  n;

    enum {
        NORMAL,
        ESCAPE,
        NAME,
        SNAME
    } state = NORMAL;

    b.pos = b.last = b.start = ngx_alloc(NGX_RTMP_EVAL_BUFLEN, log);
    if (b.pos == NULL) {
        return NGX_ERROR;
    }

    b.end = b.pos + NGX_RTMP_EVAL_BUFLEN;
    name.data = NULL;

    for (n = 0; n < in->len; ++n) {
        p = &in->data[n];
        c = *p;

        switch (state) {
            case SNAME:
                if (c != '}') {
                    continue;
                }

                name.len = p - name.data;
                ngx_rtmp_eval_append_var(ctx, &b, e, &name, log);

                state = NORMAL;

                continue;

            case NAME:
                if (c == '{' && name.data == p) {
                    ++name.data;
                    state = SNAME;
                    continue;
                }
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                    continue;
                }

                name.len = p - name.data;
                ngx_rtmp_eval_append_var(ctx, &b, e, &name, log);
                /* fall through */

            case NORMAL:
                switch (c) {
                    case '$':
                        name.data = p + 1;
                        state = NAME;
                        continue;
                    case '\\':
                        state = ESCAPE;
                        continue;
                }

                /* fall through */

            case ESCAPE:
                ngx_rtmp_eval_append(&b, &c, 1, log);
                state = NORMAL;
                break;

        }
    }

    if (state == NAME) {
        p = &in->data[n];
        name.len = p - name.data;
        ngx_rtmp_eval_append_var(ctx, &b, e, &name, log);
    }

    c = 0;
    ngx_rtmp_eval_append(&b, &c, 1, log);

    out->data = b.pos;
    out->len  = b.last - b.pos - 1;

    return NGX_OK;
}

ngx_fd_t ngx_rtmp_int_to_fd_t(ngx_int_t v)
{
    switch (v)
    {
        case 0:
            return ngx_stdin;
        case 1:
            return ngx_stdout;
        case 2:
            return ngx_stderr;
        default:
            return NGX_INVALID_FILE;
    }
}

ngx_int_t
ngx_rtmp_eval_streams(ngx_str_t *in)
{
    ngx_int_t   mode, create, v, close_src;
    ngx_fd_t    dst, src;
    u_char     *path;

    path = in->data;

    while (*path >= '0' && *path <= '9') {
        path++;
    }

    switch ((char) *path) {

        case '>':

            v = (path == in->data ? 1 : ngx_atoi(in->data, path - in->data));
            if (v == NGX_ERROR) {
                return NGX_ERROR;
            }

            if ((dst = ngx_rtmp_int_to_fd_t(v)) == NGX_INVALID_FILE) {
                return NGX_ERROR;
            }

            mode = NGX_FILE_WRONLY;
            create = NGX_FILE_TRUNCATE;
            path++;

            if (*path == (u_char) '>') {
                mode = NGX_FILE_APPEND;
                create = NGX_FILE_CREATE_OR_OPEN;
                path++;
            }

            break;

        case '<':

            v = (path == in->data ? 0 : ngx_atoi(in->data, path - in->data));
            if (v == NGX_ERROR) {
                return NGX_ERROR;
            }

            if ((dst = ngx_rtmp_int_to_fd_t(v)) == NGX_INVALID_FILE) {
                return NGX_ERROR;
            }

            mode = NGX_FILE_RDONLY;
            create = NGX_FILE_OPEN;
            path++;

            break;

        default:

            return NGX_DONE;
    }

    if (*path == (u_char) '&') {

        path++;
        v = ngx_atoi(path, in->data + in->len - path);
        if (v == NGX_ERROR) {
            return NGX_ERROR;
        }

        if ((src = ngx_rtmp_int_to_fd_t(v)) == NGX_INVALID_FILE) {
            return NGX_ERROR;
        }

        close_src = 0;

    } else {

        src = ngx_open_file(path, mode, create, NGX_FILE_DEFAULT_ACCESS);
        if (src == NGX_INVALID_FILE) {
            return NGX_ERROR;
        }
        close_src = 1;

    }

    if (src == dst) {
        return NGX_OK;
    }

    if (src == ngx_stdin) {
        ngx_set_stdin(dst);
    } else if (src == ngx_stdout) {
        ngx_set_stdout(dst);
    } else if (src == ngx_stderr) {
        ngx_set_stderr(dst);
    } else {
        return NGX_ERROR;
    }

    if (close_src) {
        ngx_close_file(src);
    }
    return NGX_OK;
}
