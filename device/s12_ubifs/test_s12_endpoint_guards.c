/* Run only architecture-independent upstream guard tests in a separate S12
 * process. The ARM32 transport tests are intentionally not invoked here. */
#define main upstream_arm32_test_not_invoked
#include "../endpoint_probe/test_first_endpoint.c"
#undef main

int main(void) {
    assert(mkdir(CONTROL_DIR,0700)==0);
    assert(dlopen("/usr/lib/libaivs_sdk.so",RTLD_NOW|RTLD_GLOBAL));
    assert(resolve_json());json_ready=1;
    first_end=fake_end;first_stop=fake_stop;first_asr=fake_audio;
    first_worker_ready=1;first_chained=fake_wake;
    no_speech_test();helper_death_test();replacement_test();
    pool_handoff_test(NP_MAX);pool_handoff_test(12);
    dispose();unlink(CONTROL_FILE);unlink(CONTROL_DIR "/events.log");
    assert(!rmdir(CONTROL_DIR));
    puts("S12_ENDPOINT_GUARDS_TEST_OK no_speech helper_death replacement pool");
    return 0;
}
