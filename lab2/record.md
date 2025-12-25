使用1.jpg，延迟为3ms

丢包率3%

窗口大小5：

[SEND] Total time: 60809 ms
[SEND] Average throughput: 0.03 MB/s



窗口大小10

[SEND] Total time: 67699 ms
[SEND] Average throughput: 0.03 MB/s



窗口大小20

[SEND] Total time: 68683 ms
[SEND] Average throughput: 0.03 MB/s



窗口大小50

[SEND] Total time: 66968 ms
[SEND] Average throughput: 0.03 MB/s

从窗口大小20开始，cwnd最大值就是10了，然后就会进入拥塞避免状态，阈值变回cwnd/2+3，怀疑是加了延迟和丢包之后cwnd等不到增大为20就会因为延迟和丢包导致接收方收到重复ack，然后reno算法的阈值腰斩，导致cwnd上不去。因此不多做尝试





窗口大小设置为50，延迟3ms，改变丢包率：

5%

[SEND] Total time: 68079 ms
[SEND] Average throughput: 0.03 MB/s



10%

[SEND] Total time: 73745 ms
[SEND] Average throughput: 0.02 MB/s



15%

[SEND] Total time: 86597 ms
[SEND] Average throughput: 0.02 MB/s



20%



[SEND] Total time: 302422 ms
[SEND] Average throughput: 0.01 MB/s