// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_input_item.h>
#include <vlc_preparser.h>

struct context {
    vlc_sem_t done;
    int status;
    unsigned subtrees;
    unsigned children;
    bool original_root;
    input_item_node_t *subtree;
};

static void on_subtree(vlc_preparser_req *req, input_item_node_t *subtree,
                       void *userdata)
{
    struct context *ctx = userdata;
    ctx->subtrees++;
    ctx->children += subtree->i_children;
    ctx->original_root = subtree->p_item == vlc_preparser_req_GetItem(req);
    if (ctx->subtree)
        input_item_node_Delete(ctx->subtree);
    ctx->subtree = subtree;
}

static void on_ended(vlc_preparser_req *req, int status, void *userdata)
{
    (void)req;
    struct context *ctx = userdata;
    ctx->status = status;
    vlc_sem_post(&ctx->done);
}

int main(void)
{
    test_init();
    const char *args[] = {"--ignore-config", "--no-plugins-cache",
        "--no-media-library", "--no-metadata-network-access"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance);
    bool passed = true;
    for (unsigned external = 0; external < 2; ++external) {
#ifndef HAVE_VLC_EXTERNAL_PREPARSER
        if (external) break;
#endif
        const struct vlc_preparser_cfg cfg = {
            .types = VLC_PREPARSER_TYPE_PARSE,
            .max_parser_threads = 1,
            .timeout = VLC_TICK_FROM_SEC(5),
            .external_process = external,
        };
        vlc_preparser_t *parser = vlc_preparser_New(VLC_OBJECT(instance->p_libvlc_int), &cfg);
        assert(parser);
        input_item_t *item = input_item_New("mock://node_count=2;length=1000000", "Root");
        assert(item);
        struct context ctx = { .status = VLC_EGENERIC };
        vlc_sem_init(&ctx.done, 0);
        static const struct vlc_preparser_cbs cbs = {
            .on_ended = on_ended,
            .on_subtree_added = on_subtree,
        };
        vlc_preparser_req *req = vlc_preparser_req_NewParse(parser, item,
            VLC_PREPARSER_TYPE_PARSE | VLC_PREPARSER_OPTION_SUBITEMS, &cbs, &ctx);
        assert(req);
        assert(vlc_preparser_Submit(parser, req) == VLC_SUCCESS);
        vlc_sem_wait(&ctx.done);
        vlc_preparser_req_Release(req);
        vlc_preparser_Delete(parser);
        const bool success = ctx.status == VLC_SUCCESS && ctx.subtrees == 1
            && ctx.children == 2 && ctx.original_root;
        fprintf(stderr, "subtree external=%u status=%d subtrees=%u children=%u original-root=%d\n",
            external, ctx.status, ctx.subtrees, ctx.children, ctx.original_root);
        passed &= success;
        input_item_Release(item);
        if (ctx.subtree) {
            char *uri = input_item_GetURI(ctx.subtree->p_item);
            assert(uri && !strcmp(uri, "mock://node_count=2;length=1000000"));
            free(uri);
            input_item_node_Delete(ctx.subtree);
        }
    }
    libvlc_release(instance);
    return passed ? 0 : 1;
}
