#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "barcode_app.h"
#include "fake_port.h"
/* The documented wire frames for the four commands design section 5 accepts. */
enum { T_YELLOW = 1, T_GREEN, T_RED, T_PASS };
static const uint8_t cmd_wire[5][2] = {
    {0, 0},
    {HOST_CMD_LIGHT, 0x2Fu},
    {HOST_CMD_LIGHT, 0x31u},
    {HOST_CMD_LIGHT, HOST_PARAM_RED_FAIL},
    {HOST_CMD_PASS, HOST_ACK_OK}};
static Barcode a={3,{'A',' ','b'}}, b={3,{'A',' ','B'}};
static AppSnapshot s;
static void start(void){FakePort_Reset();BarcodeApp_Init(0);}
static void snapshot(void){BarcodeApp_GetSnapshot(&s);}
static void send_cmd(unsigned which, uint32_t t)
{
    HostFrame f = { true, HOST_ADDR_BOX, cmd_wire[which][0], cmd_wire[which][1], true };
    BarcodeApp_OnFrame(&f, t);
}
/* <7> query: addr=02, cmd=0x17, no param byte. */
static void send_sn(uint32_t t)
{
    HostFrame f = { true, HOST_ADDR_SN, HOST_CMD_SN, 0, false };
    BarcodeApp_OnFrame(&f, t);
}
static void test_sn(void)
{
    unsigned host;
    Barcode sn = {2, {'A', 'B'}};
    /* Nothing in flight: failure ACK, no payload frame. */
    start();
    host = fake_host_count;
    send_sn(5);
    assert(fake_host_count == host + 1u);
    assert(fake_host[host].len == 7u);
    assert(memcmp(fake_host[host].data, "\xAB\x07\xFA\x02\x0E\x25\x85", 7) == 0);
    /* Locked barcode: success ACK, then the real bytes. */
    start();
    BarcodeApp_OnBarcode(&sn, 10);
    host = fake_host_count;
    send_sn(20);
    assert(fake_host_count == host + 2u);
    assert(memcmp(fake_host[host].data, "\xAB\x07\xFA\x02\x0A\x65\x01", 7) == 0);
    assert(fake_host[host + 1u].len == 9u);
    assert(memcmp(fake_host[host + 1u].data,
                  "\xAB\x09\xFA\x02\x17\x41\x42\x33\x14", 9) == 0);
    snapshot();
    assert(s.state == APP_WAIT_RESULT && s.current.len == 2u);
    /* Longest barcode: the reply must fit and carry the payload intact. */
    {
        Barcode full = {0, {0}};
        unsigned k;
        for (k = 0; k < BARCODE_MAX_LEN; ++k)
            full.data[k] = (uint8_t)('A' + (int)(k % 26));
        full.len = BARCODE_MAX_LEN;
        start();
        BarcodeApp_OnBarcode(&full, 10);
        host = fake_host_count;
        send_sn(20);
        assert(fake_host[host + 1u].len == HOST_TX_MAX);
        assert(memcmp(fake_host[host + 1u].data + 5u, full.data, BARCODE_MAX_LEN) == 0);
        assert(HostCrc16(fake_host[host + 1u].data, (uint8_t)(HOST_TX_MAX - 2u)) ==
               (uint16_t)((uint16_t)fake_host[host + 1u].data[HOST_TX_MAX - 2u] << 8
                          | fake_host[host + 1u].data[HOST_TX_MAX - 1u]));
    }
    /* Live through the release window, then expires with the state. */
    start();
    BarcodeApp_OnBarcode(&sn, 100);
    send_cmd(T_PASS, 101);
    host = fake_host_count;
    send_sn(102);
    assert(fake_host_count == host + 2u);
    BarcodeApp_Tick(1102);
    host = fake_host_count;
    send_sn(1202);
    assert(fake_host_count == host + 1u && fake_host[host].data[4] == HOST_ACK_FAIL);
    /* The query must not touch lights, the relay or the upload stream. */
    start();
    BarcodeApp_OnBarcode(&sn, 10);
    fake_light_changes = 0;
    host = fake_host_count;
    send_sn(30);
    assert(fake_light_changes == 0u && !fake_relay && fake_host_count == host + 2u);
    send_sn(40);
    assert(fake_host_count == host + 4u && fake_light_changes == 0u && !fake_relay);
}
static void check_ack(unsigned index){assert(fake_host[index].len==7);assert(memcmp(fake_host[index].data,BARCODE_ACK,7)==0);}
static void assert_off(void){assert(!fake_red&&!fake_green&&!fake_yellow);}
/* <1> version query: ACK1 then a one-byte version ACK2, in every business
   state, without touching the panel, the relay or the upload stream. */
static void test_version(void)
{
    unsigned host, st;
    HostFrame f = { true, HOST_ADDR_BOX, HOST_CMD_VERSION, 0, false };

    for (st = 0; st < 3; ++st) {
        start();
        if (st) BarcodeApp_OnBarcode(&a, 15);
        if (st == 2) send_cmd(T_PASS, 10);
        fake_light_changes = 0;
        host = fake_host_count;
        BarcodeApp_OnFrame(&f, 20);
        assert(fake_host_count == host + 2u);
        check_ack(host);
        assert(fake_host[host + 1u].len == 8u);
        assert(memcmp(fake_host[host + 1u].data, "\xAB\x08\xFA\x01\xFF\x65\xD1\x9D", 8) == 0);
        assert(HostCrc16(fake_host[host + 1u].data, 6u) == 0xD19Du);
        assert(fake_scanner_count == 0u);
        snapshot();
        if (st == 0) assert(s.state == APP_IDLE && s.count == 0u && !fake_relay);
        if (st == 1) assert(s.state == APP_WAIT_RESULT && s.count == 0u);
        if (st == 2) assert(s.state == APP_RELEASING && s.count == 1u && fake_relay);
        assert(fake_light_changes == 0u);
    }
    /* It is not a business event, so a manual lamp override survives it. */
    start();
    BarcodeApp_OnBarcode(&a, 10);
    send_cmd(T_RED, 20);
    BarcodeApp_Tick(600);
    assert(!fake_red);
    host = fake_host_count;
    BarcodeApp_OnFrame(&f, 610);
    assert(fake_host_count == host + 2u);
    BarcodeApp_Tick(1100);
    assert(fake_red && !fake_green && !fake_yellow);
    snapshot();
    assert(s.state == APP_IDLE && s.view == VIEW_FAILED);
    /* Off-address for the version command is dropped without any reply. */
    start();
    host = fake_host_count;
    f.addr = HOST_ADDR_SN;
    BarcodeApp_OnFrame(&f, 5);
    assert(fake_host_count == host);
}
/* FF is the one lamp command that lights all three at once, in sync, and it
   is still a manual override that the next business event takes back. */
static void test_all_blink(void)
{
    HostFrame f = { true, HOST_ADDR_BOX, HOST_CMD_LIGHT, 0xFFu, true };
    unsigned host;
    start();
    host = fake_host_count;
    BarcodeApp_OnFrame(&f, 10);
    check_ack(host);
    assert(fake_red && fake_green && fake_yellow);
    BarcodeApp_Tick(510);
    assert(!fake_red && !fake_green && !fake_yellow);
    BarcodeApp_Tick(1010);
    assert(fake_red && fake_green && fake_yellow);
    BarcodeApp_OnBarcode(&a, 1500);
    assert(fake_yellow && !fake_green && !fake_red);
    snapshot();
    assert(s.state == APP_WAIT_RESULT);
    f.param = 0x00u;
    host = fake_host_count;
    BarcodeApp_OnFrame(&f, 2000);
    check_ack(host);
    assert(!fake_red && !fake_green && !fake_yellow);
    snapshot();
    assert(s.state == APP_WAIT_RESULT);
}
static void test_flow(void){
 unsigned host;
 start();assert(!fake_relay && fake_light_changes==0);assert_off();
 BarcodeApp_OnBarcode(&a,1);snapshot();assert(s.state==APP_WAIT_RESULT);
 assert(fake_host_count==1 && fake_host[0].len==5 && memcmp(fake_host[0].data,"A b\r\n",5)==0);
 assert(fake_yellow&&!fake_green&&!fake_red);
 BarcodeApp_OnBarcode(&a,2);BarcodeApp_OnBarcode(&b,3);assert(fake_host_count==1 && fake_yellow);
 send_cmd(T_GREEN,4);check_ack(1);snapshot();assert(s.state==APP_WAIT_RESULT && s.count==0 && !fake_relay);
 assert(fake_green&&!fake_yellow&&!fake_red);
 BarcodeApp_Tick(100000);snapshot();assert(s.state==APP_WAIT_RESULT && fake_scanner_count==0 && fake_green);
 send_cmd(T_PASS,100010);snapshot();assert(s.state==APP_RELEASING && s.count==1 && fake_relay && fake_green);
 check_ack(2);host=fake_host_count;
 BarcodeApp_OnBarcode(&a,100100);BarcodeApp_OnBarcode(&b,100200);assert(fake_host_count==host && fake_green);
 send_cmd(T_PASS,100500);check_ack(host);
 assert(fake_green && fake_relay);
 send_cmd(T_RED,100600);check_ack(host+1);
 assert(fake_relay && fake_red && !fake_green && fake_scanner_count==0);
 BarcodeApp_Tick(101009);assert(fake_relay && fake_red);
 BarcodeApp_Tick(101010);snapshot();assert(!fake_relay && s.state==APP_IDLE);assert_off();
 host=fake_host_count;BarcodeApp_OnBarcode(&a,101020);snapshot();assert(s.view==VIEW_DUPLICATE && s.count==1 && fake_host_count==host && fake_scanner_count==0);assert_off();
 BarcodeApp_OnBarcode(&b,101030);snapshot();assert(s.state==APP_WAIT_RESULT && fake_yellow);
 send_cmd(T_RED,101100);snapshot();assert(s.state==APP_IDLE && s.view==VIEW_FAILED && s.count==1 && fake_scanner_count==1);
 assert(memcmp(fake_scanner[0].data,BARCODE_RESCAN,3)==0);
 assert(fake_red&&!fake_green&&!fake_yellow);
 send_cmd(T_RED,101101);assert(fake_scanner_count==1 && fake_red);
 BarcodeApp_OnBarcode(&b,101110);snapshot();assert(s.state==APP_WAIT_RESULT && s.current.data[2]=='B' && fake_yellow);
}
static void test_matrix(void){unsigned state,cmd;for(state=0;state<3;state++)for(cmd=1;cmd<=4;cmd++){
 unsigned before;start();if(state)BarcodeApp_OnBarcode(&a,15);if(state==2)send_cmd(T_PASS,10);
 before=fake_host_count;send_cmd(cmd,20);assert(fake_host_count==before+1);check_ack(before);snapshot();
 if(state==0)assert(s.state==APP_IDLE && s.count==0 && !fake_relay && fake_scanner_count==0);
 if(state==1 && cmd==T_PASS)assert(s.state==APP_RELEASING && s.count==1 && fake_relay && fake_green);
 if(state==1 && cmd==T_RED)assert(s.state==APP_IDLE && s.count==0 && fake_scanner_count==1 && fake_red);
 if(state==1 && cmd<T_RED)assert(s.state==APP_WAIT_RESULT && s.count==0);
 if(state==2){assert(s.state==APP_RELEASING && s.count==1 && fake_scanner_count==0);BarcodeApp_Tick(1010);assert(!fake_relay);assert_off();}
}}
static void test_lights_time(void){start();BarcodeApp_OnBarcode(&a,10);
 assert(fake_yellow);
 send_cmd(T_YELLOW,10);assert(fake_yellow);
 BarcodeApp_Tick(509);assert(fake_yellow);BarcodeApp_Tick(510);assert(!fake_yellow);
 BarcodeApp_Tick(1010);assert(fake_yellow);BarcodeApp_Tick(2510);assert(!fake_yellow);
 send_cmd(T_GREEN,2511);BarcodeApp_Tick(3010);assert(fake_green&&!fake_yellow&&!fake_red);
 send_cmd(T_RED,3011);assert(fake_red&&!fake_green&&!fake_yellow);
 start();BarcodeApp_OnBarcode(&a,UINT32_MAX-500u);send_cmd(T_PASS,UINT32_MAX-499u);
 assert(fake_green);BarcodeApp_Tick(499);assert(fake_relay&&fake_green);BarcodeApp_Tick(500);assert(!fake_relay&&!fake_green);
 start();BarcodeApp_OnBarcode(&a,15);send_cmd(T_PASS,20);
 BarcodeApp_AdvanceTime(15);assert(fake_relay); /* historical event must not underflow timer */
 BarcodeApp_Tick(1019);assert(fake_relay);BarcodeApp_Tick(1020);assert(!fake_relay&&!fake_green);
}
static void test_auto_lights(void){Barcode c={3,{'C','x','1'}};start();assert_off();
 BarcodeApp_OnBarcode(&a,10);
 assert(fake_yellow&&!fake_green&&!fake_red && fake_light_changes==1);
 BarcodeApp_Tick(510);assert(!fake_yellow);
 BarcodeApp_Tick(1010);assert(fake_yellow);
 send_cmd(T_PASS,1020);
 assert(fake_green&&!fake_yellow&&!fake_red && fake_relay);
 BarcodeApp_Tick(2019);assert(fake_green && fake_relay);
 BarcodeApp_Tick(2020);assert(!fake_relay);assert_off();
 snapshot();assert(s.state==APP_IDLE && s.count==1);
 BarcodeApp_OnBarcode(&b,3000);
 assert(fake_yellow&&!fake_green&&!fake_red);
 send_cmd(T_RED,3010);
 snapshot();assert(s.state==APP_IDLE && s.view==VIEW_FAILED && s.count==1);
 assert(fake_red&&!fake_green&&!fake_yellow);
 BarcodeApp_Tick(3510);assert(!fake_red);
 BarcodeApp_Tick(4010);assert(fake_red);
 BarcodeApp_OnBarcode(&b,4020);
 assert(fake_yellow&&!fake_green&&!fake_red);
 send_cmd(T_PASS,4030);
 assert(fake_green&&!fake_yellow&&!fake_red && fake_relay);
 BarcodeApp_Tick(5030);assert(!fake_relay);assert_off();
 snapshot();assert(s.state==APP_IDLE && s.count==2);
 BarcodeApp_OnBarcode(&c,5200);
 send_cmd(T_GREEN,5210);
 assert(fake_green&&!fake_yellow&&!fake_red);
 BarcodeApp_Tick(6210);assert(fake_green);
 send_cmd(T_RED,6220);assert(fake_red&&!fake_green);
 BarcodeApp_OnBarcode(&c,6300);assert(fake_yellow&&!fake_green&&!fake_red);
 send_cmd(T_PASS,6310);assert(fake_green && fake_relay);
 BarcodeApp_Tick(7310);assert_off();
 snapshot();assert(s.state==APP_IDLE && s.count==3);
 BarcodeApp_OnBarcode(&c,8000);
 snapshot();assert(s.view==VIEW_DUPLICATE);assert_off();
}
static void test_fifo_reset_fault(void){unsigned i,host;Barcode code={2,{0,0}};start();
 for(i=0;i<151;i++){code.data[0]=(uint8_t)i;BarcodeApp_OnBarcode(&code,i*1100);send_cmd(T_PASS,i*1100+1);BarcodeApp_Tick(i*1100+1001);}
 snapshot();assert(s.count==150);host=fake_host_count;code.data[0]=1;BarcodeApp_OnBarcode(&code,160000);assert(fake_host_count==host);
 code.data[0]=0;BarcodeApp_OnBarcode(&code,160010);assert(fake_host_count==host+1);
 BarcodeApp_Init(200000);snapshot();assert(s.count==0 && s.state==APP_IDLE && !fake_relay);
 start();fake_send_ok=false;BarcodeApp_OnBarcode(&a,5);snapshot();assert(s.comm_fault && s.state==APP_WAIT_RESULT);
 fake_send_ok=true;send_cmd(T_PASS,10);assert(!fake_relay);snapshot();assert(s.count==0);
 start();BarcodeApp_OnBarcode(&a,5);send_cmd(T_PASS,10);BarcodeApp_CommunicationFault();BarcodeApp_Tick(1010);
 snapshot();assert(!fake_relay && s.comm_fault);host=fake_host_count;BarcodeApp_OnBarcode(&b,20);assert(fake_host_count==host);
}
static void test_boot(void){
 unsigned t;Barcode invalid={0,{0}};
 start();BarcodeApp_Init(2000);
 BarcodeApp_Tick(2299);assert(fake_scanner_count==0);
 BarcodeApp_Tick(2300);assert(fake_scanner_count==1);
 BarcodeApp_Tick(2399);assert(fake_scanner_count==1);
 BarcodeApp_Tick(2400);assert(fake_scanner_count==2);
 BarcodeApp_Tick(2500);assert(fake_scanner_count==3);
 BarcodeApp_Tick(9000);assert(fake_scanner_count==3);
 for(t=0;t<3;t++){
  start();if(t>=1)BarcodeApp_Tick(300);if(t>=2)BarcodeApp_Tick(400);
  BarcodeApp_OnBarcode(&a,500);BarcodeApp_Tick(1000);assert(fake_scanner_count==t);
  send_cmd(T_RED,1100);assert(fake_scanner_count==t+1);
  BarcodeApp_Tick(9000);assert(fake_scanner_count==t+1);
 }
 start();BarcodeApp_OnBarcode(&invalid,290);BarcodeApp_Tick(300);assert(fake_scanner_count==1);
 start();BarcodeApp_Init(UINT32_MAX-99u);BarcodeApp_Tick(199);assert(fake_scanner_count==0);
 BarcodeApp_Tick(200);assert(fake_scanner_count==1);BarcodeApp_Tick(300);assert(fake_scanner_count==2);BarcodeApp_Tick(400);assert(fake_scanner_count==3);
 start();BarcodeApp_Tick(800);assert(fake_scanner_count==1);BarcodeApp_Tick(800);assert(fake_scanner_count==1);
 BarcodeApp_Tick(900);assert(fake_scanner_count==2);snapshot();assert(s.boot_late>0);
 start();fake_send_ok=false;BarcodeApp_Tick(300);snapshot();assert(s.comm_fault && s.boot_sent==0);
}
int main(void){test_flow();test_matrix();test_lights_time();test_auto_lights();test_fifo_reset_fault();test_boot();test_sn();test_version();test_all_blink();puts("PASS app: state/command matrix, raw upload, FIFO, relay/lights/wrap, auto lights, communication fault, SN query, version query, all-blink");return 0;}
