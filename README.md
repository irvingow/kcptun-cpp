### kcptun-cpp: kcptun cpp version

#### usage:
> mkdir build

>cd build

if you want to compile in debug mode:
> cmake -DCMAKE_BUILD_TYPE=Debug ..

compile in release mode:
> cmake -DCMAKE_BUILD_TYPE=Release ..

#### introduction:
reimplement kcptun in c++.

data flow:
client_tcp_pack->kcp->fec_encode->udp->fec_decode->kcp->server_tcp_pack

server_tcp_pack->kcp->fec_encode->udp->fec_decode->kcp->client_tcp_pack
