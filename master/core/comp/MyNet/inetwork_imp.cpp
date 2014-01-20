#include "StdAfx.h"
#include "inetwork_imp.h"
#include <process.h>

#include "inet_tcp_handler.h"
#include "net_conn.h"
#include <net/addr_resolver.h>
#include <net/winsockInit.h>

#include <BaseTsd.h>
#include <MSWSock.h>

#define  MAX_BUF_SIZE 1024

#if (_MSC_VER == 1200)
typedef
    BOOL
    (PASCAL FAR * LPFN_CONNECTEX) (
    SOCKET s,
    const struct sockaddr FAR *name,
    int namelen,
    PVOID lpSendBuffer,
    DWORD dwSendDataLength,
    LPDWORD lpdwBytesSent,
    LPOVERLAPPED lpOverlapped
    );

#define WSAID_CONNECTEX \
{0x25a207b9,0xddf3,0x4660,{0x8e,0xe9,0x76,0xe5,0x8c,0x74,0x06,0x3e}}

#endif

klib::net::winsock_init g_winsock_init_;

inetwork_imp::inetwork_imp(void)
{
    m_lpfnAcceptEx = NULL;
    hiocp_ = INVALID_HANDLE_VALUE;

    //��ʼ����
    init_fixed_overlapped_list(100);
}

inetwork_imp::~inetwork_imp(void)
{
}

bool inetwork_imp::init_network(inet_tcp_handler* handler, size_t thread_num/* = 1*/)
{
    // ������ɶ˿�
    hiocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE,NULL,(ULONG_PTR)0, 0);
    _ASSERT(hiocp_);
    if (NULL == hiocp_) 
    {
        return false;
    }
    
    // �����¼�������
    _ASSERT(handler);
    net_event_handler_ = handler;

    thread_num_ = thread_num;

    return true;
}

bool inetwork_imp::run_network() 
{
    work_threads_.resize(thread_num_);
    for (auto itr = work_threads_.begin(); itr != work_threads_.end(); ++ itr)
    {
        itr->start(std::bind(&inetwork_imp::worker_thread_, this, std::tr1::placeholders::_1), 0);
    }

    tmr_mgr_.start();

    return true;
}

bool inetwork_imp::try_write(net_conn* pconn, const char* buff, size_t len)
{
    // �Ƚ�Ҫ���͵����ݷ�������(֧�ֶ��߳�)
    guard guard_(pconn->get_lock());
    pconn->write_send_stream(buff, len);

    //@todo ��Ҫ�������ʣ����Ƿ���Ҫ��������
    if (pconn->get_post_write_count() > 0) {
        return true;
    }

    // һ�������ݿ��Է���
    KLIB_ASSERT(pconn->get_send_length() > 0);

    // �ȷ������е�����
    auto seg_ptr = pconn->get_send_stream().get_read_seg_ptr();
    auto seg_len = pconn->get_send_stream().get_read_seg_len();

    return this->post_placement_write(pconn, (char*)seg_ptr, seg_len);
}

bool inetwork_imp::try_read(net_conn* pconn) 
{
    // @todo Ҳ��Ҫ���ƿͻ��˷��͵����ʣ�����ﵽ��������ͣ����,��ʱ�ָ����ͣ�
    guard guard_(pconn->get_lock());
    if (pconn->get_post_read_count() > 0) {
        return true;
    }

    return this->post_read(pconn);
}

net_conn* inetwork_imp::try_listen(USHORT local_port) 
{
    net_conn* pconn =  this->create_listen_conn(local_port);
    if (NULL == pconn) {
        return NULL;
    }

    this->post_accept(pconn);
    return pconn;
}

net_conn* inetwork_imp::try_connect(const char* addr, USHORT uport, void* bind_key) 
{
    if (NULL == addr) {
        return NULL;
    }

    net_conn* pconn = create_conn();
    if (NULL == pconn) {
        return NULL;
    }

    pconn->set_bind_key(bind_key);
    pconn->set_peer_addr_str(addr);
    pconn->set_peer_port(uport);
    this->post_connection(pconn);

    return pconn;
}

//----------------------------------------------------------------------
// 
net_conn* inetwork_imp::post_accept(net_conn* pListenConn) 
{
    HANDLE hResult;
    DWORD dwBytes = 0;
    net_overLapped* lpOverlapped = get_net_overlapped();
    if (lpOverlapped == NULL) {
        return false;
    }

    SOCKET sockAccept = WSASocket(AF_INET,SOCK_STREAM, IPPROTO_TCP, NULL, NULL,WSA_FLAG_OVERLAPPED);
    if (sockAccept == INVALID_SOCKET) {
        release_net_overlapped(lpOverlapped);
        //TRACE(TEXT("Error at socket(): %ld\n"));
        return NULL;
    }

    if (NULL == m_lpfnAcceptEx) {
        GUID GuidAcceptEx = WSAID_ACCEPTEX;
        DWORD irt = WSAIoctl(sockAccept,
            SIO_GET_EXTENSION_FUNCTION_POINTER,
            &GuidAcceptEx,
            sizeof(GuidAcceptEx),
            &m_lpfnAcceptEx,
            sizeof(m_lpfnAcceptEx),
            &dwBytes,
            NULL, 
            NULL);

        if (irt == SOCKET_ERROR)
        {
            closesocket(sockAccept);
            release_net_overlapped(lpOverlapped);
            //TRACE(TEXT("fail at WSAIoctl-AcceptEx,Error Code:%d\n"));
            return NULL;
        }
    }

    net_conn* pNewConn = create_conn();
    if (NULL == pNewConn) {
        closesocket(sockAccept);
        release_net_overlapped(lpOverlapped);
        return NULL;
    }

    hResult = CreateIoCompletionPort((HANDLE)sockAccept, hiocp_, (ULONG_PTR)pNewConn, 0);
    if (NULL == hResult) {
        closesocket(sockAccept);
        release_net_overlapped(lpOverlapped);
        release_conn(pNewConn);
        _ASSERT(FALSE);
        return NULL;
    }

    // �Կ��Ե���getpeername��ȡ�׽�����Ϣ
    BOOL bSetUpdate = TRUE;
    setsockopt(sockAccept,
        SOL_SOCKET,
        SO_UPDATE_ACCEPT_CONTEXT,
        (char *)& bSetUpdate,
        sizeof(bSetUpdate) );

    pNewConn->get_socket() = sockAccept;
    lpOverlapped->operate_type_ = OP_ACCEPT;
    lpOverlapped->pend_data_ = (void*) pNewConn;  // ����Ƚ����⣬�����������ͬ
    lpOverlapped->hEvent = NULL;

    BOOL brt = m_lpfnAcceptEx(pListenConn->get_socket(),
        sockAccept,
        lpOverlapped->recv_buff_, 
        0,//lpToverlapped->dwBufSize,
        sizeof(sockaddr_in) + 16, 
        sizeof(sockaddr_in) + 16, 
        &(lpOverlapped->transfer_bytes_), 
        (LPOVERLAPPED)lpOverlapped);


    if(brt == FALSE && WSA_IO_PENDING != WSAGetLastError()) 
    {
        closesocket(sockAccept);
        release_net_overlapped(lpOverlapped);
        release_conn(pNewConn);

        DWORD dwErrorCode = WSAGetLastError();
        //TRACE(TEXT("fail at lpfnAcceptEx,error code:%d\n"), dwErrorCode);
        return NULL;
    }
    else if (WSA_IO_PENDING == WSAGetLastError()) 
    {
        //�ɹ�
        return pNewConn;
    }

    //�ɹ�
    return pNewConn;
}

bool inetwork_imp::post_connection(net_conn* pConn) 
{
    _ASSERT(pConn);

    UINT32  uPeerAddr = 0;
    klib::net::addr_resolver ip_resolver_(pConn->get_peer_addr_str());
    if (ip_resolver_.size() < 0)  
    {
        return false;
    }
    uPeerAddr = ip_resolver_.at(0);

    SOCKET& sock = pConn->get_socket();
    sock = ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);
    if (sock == INVALID_SOCKET) 
    {
        _ASSERT(FALSE);  // DWORD dwErr = WSAGetLastError();
        _ASSERT(!"�����׽���ʧ��!!!");
        return false;
    }

    sockaddr_in local_addr;
    ZeroMemory(&local_addr, sizeof(sockaddr_in));
    local_addr.sin_family = AF_INET;
    int iret = ::bind(sock, (sockaddr*)(&local_addr), sizeof(sockaddr_in));
    if (SOCKET_ERROR == iret) 
    {
        closesocket(sock);
        _ASSERT(!"�󶨶˿�ʧ��!!!");
        return false;
    }

    HANDLE hResult = CreateIoCompletionPort((HANDLE)sock,hiocp_, (ULONG_PTR)pConn,0);
    if (NULL == hResult) 
    {
        closesocket(sock);
        _ASSERT(FALSE);
        return false;
    }

    LPFN_CONNECTEX m_lpfnConnectEx = NULL;
    DWORD dwBytes = 0;
    GUID GuidConnectEx = WSAID_CONNECTEX;

    // �ص㣬���ConnectEx������ָ��
    if (SOCKET_ERROR == WSAIoctl(sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
        &GuidConnectEx, sizeof (GuidConnectEx),
        &m_lpfnConnectEx, sizeof (m_lpfnConnectEx), &dwBytes, 0, 0))
    {
        closesocket(sock);
        //TRACE( TEXT("WSAIoctl is failed. Error code = %d"));//, WSAGetLastError());
        _ASSERT(FALSE);
        return FALSE;
    }

    net_overLapped *pmyoverlapped = get_net_overlapped(); // socket��I/OͨѶ������
    if (NULL == pmyoverlapped) 
    {
        _ASSERT(FALSE && "TODO ����Ҫ�ͷ���Դ��");
        return false;
    }
    pmyoverlapped->operate_type_ = OP_CONNECT;  // �����������ͣ��õ�I/O���ʱ���ݴ�                                   //                                         //��ʶ����������
    pmyoverlapped->hEvent = NULL;  // ���¼�ģ��

    // ��������Ŀ���ַ
    sockaddr_in addrPeer;  
    ZeroMemory(&addrPeer, sizeof(sockaddr_in));
    addrPeer.sin_family = AF_INET;
    addrPeer.sin_addr.s_addr = uPeerAddr;
    addrPeer.sin_port = htons( pConn->get_peer_port() );

    int nLen = sizeof(addrPeer);
    PVOID lpSendBuffer = NULL;
    DWORD dwSendDataLength = 0;
    DWORD dwBytesSent = 0;

    //�ص�
    BOOL bResult = m_lpfnConnectEx(sock,
        (sockaddr*)&addrPeer,  // [in] �Է���ַ
        nLen,  				// [in] �Է���ַ����
        lpSendBuffer,		// [in] ���Ӻ�Ҫ���͵����ݣ����ﲻ��
        dwSendDataLength,	// [in] �������ݵ��ֽ��� �����ﲻ��
        &dwBytesSent,		// [out] �����˶��ٸ��ֽڣ����ﲻ��
        (OVERLAPPED*)pmyoverlapped); // [in] �ⶫ�����ӣ���һƪ�����

    if (!bResult )  // ����ֵ����
    {
        if( WSAGetLastError() != ERROR_IO_PENDING) 
        { 
            // ����ʧ��
            closesocket(sock);
            _ASSERT(FALSE);
            return FALSE;
        }
        else 
        {//����δ�������ڽ����С���
            //TRACE(TEXT("WSAGetLastError() == ERROR_IO_PENDING\n"));// �������ڽ�����
        }
    }

    return true;
}

bool inetwork_imp::post_read(net_conn* pConn) 
{
    DWORD dwRecvedBytes = 0;
    DWORD dwFlag = 0;
    DWORD transfer_bytes_ = 0;
    net_overLapped* pMyoverlapped = get_net_overlapped();
    if (NULL == pMyoverlapped) 
    {
        _ASSERT(pMyoverlapped);
        return false;
    }

    pMyoverlapped->hEvent = NULL;	// 
    pMyoverlapped->operate_type_ = OP_READ ; // ������
    pMyoverlapped->wsaBuf_.buf = pMyoverlapped->recv_buff_;
    pMyoverlapped->wsaBuf_.len = sizeof(pMyoverlapped->recv_buff_);

    int nResult = WSARecv (pConn->get_socket(),  // �Ѿ���IOCP������socket
        &pMyoverlapped->wsaBuf_,			// �������ݵ�WSABUF�ṹ
        1,
        &transfer_bytes_, // ��������ɣ��򷵻ؽ��յõ����ֽ���
        &dwFlag,
        (OVERLAPPED*)pMyoverlapped,  //�����ﴫ��OVERLADDED�ṹ
        NULL);
    if (SOCKET_ERROR  == nResult) 
    {
        if (WSAGetLastError() ==  WSA_IO_PENDING) 
        {
        }
        else 
        {
            release_net_overlapped(pMyoverlapped);

            _ASSERT(net_event_handler_);
            check_and_disconnect(pConn);
            return false;
        }
    }

    guard guard_(pConn->get_lock());
    pConn->inc_post_read_count();
    return true;
}

bool inetwork_imp::post_placement_write(net_conn* pconn, char* buff, size_t len) 	///< Ͷ��д����
{
    // �����ύ��������
    DWORD dwWriteLen = 0;
    net_overLapped *pmyoverlapped = get_net_overlapped();
    if(pmyoverlapped == NULL) 
    {
        return false;
    }

    pmyoverlapped->operate_type_ = OP_WRITE;  // ����I/O��������
    pmyoverlapped->wsaBuf_.buf = buff;
    pmyoverlapped->wsaBuf_.len = len;

    int nResult = WSASend(                  // ��ʼ����
        pconn->get_socket(),                // �����ӵ�socket
        &pmyoverlapped->wsaBuf_,            // ����buf�����ݳ���
        1,
        &dwWriteLen,                        // ��������ɣ��򷵻ط��ͳ���
        0,	                                // 
        (LPWSAOVERLAPPED)pmyoverlapped,     // OVERLAPPED �ṹ
        0);                                 // ���̣�����

    if (nResult == SOCKET_ERROR)
    {
        int iErrorCode = WSAGetLastError();

        if (iErrorCode ==  WSA_IO_PENDING)
        {
            //TRACE(TEXT("iErrorCode ==  WSA_IO_PENDING\n"));
        }
        else if (iErrorCode == WSAEWOULDBLOCK)
        {
            ///TRACE(TEXT("iErrorCode == WSAEWOULDBLOCK\n"));
        }
        else 
        {
            return false;
        }
    }
    else 
    {
        //TRACE(TEXT("���������"));
    }

    guard guard_(pconn->get_lock());
    pconn->inc_post_write_count();
    return true;
}

//----------------------------------------------------------------------
// 
void inetwork_imp::on_read(net_conn* pConn, const char* buff, DWORD dwByteTransfered)
{
    {
        guard guard_(pConn->get_lock());
        pConn->dec_post_read_count();
    }

    if (dwByteTransfered == 0) 
    {
        // _ASSERT(FALSE && "��������ǶԷ�ֱ�ӹر�������");
        this->check_and_disconnect(pConn);
    }
    else 
    {
        {
            guard guard_(pConn->get_lock());

            // д�뵽����
            pConn->write_recv_stream(buff, dwByteTransfered);
        }

        // ͳ�Ƹ��׽��ֵĶ��ֽ���
        pConn->add_readed_bytes(dwByteTransfered);

        // ֪ͨ�ϲ㴦�����¼�
        net_event_handler_->on_read(pConn, buff, dwByteTransfered);

        // ����Ͷ�ݶ�����
        this->post_read(pConn);
    }

    // ���������ٸ��»�Ծʱ�䣬�Ա���Լ�����ӷ������ݵ��ٶ�
    pConn->upsate_last_active_tm();
}

void inetwork_imp::on_write(net_conn* pconn, DWORD dwByteTransfered)
{
    {// ��������
        guard guard_(pconn->get_lock());

        // �����д�˵�����
        pconn->mark_send_stream(dwByteTransfered);

        // ���ٸ��׽����ϵ�дͶ�ݼ���
        pconn->dec_post_write_count();

        // ͳ�Ƹ��׽��ֵ�д�ֽ���
        pconn->add_rwited_bytes(dwByteTransfered);
    }

    // ֪ͨ�ϲ㴦��д�¼�
    net_event_handler_->on_write(pconn, dwByteTransfered);

    // �����ϴλ�Ծ��ʱ���
    pconn->upsate_last_active_tm();

    {// ��������
        guard guard_(pconn->get_lock());

        // ���ŷ���û�з����������
        if (pconn->get_send_length() > 0) 
        {
            // �ٷ������е�����
            auto seg_ptr = pconn->get_send_stream().get_read_seg_ptr();
            auto seg_len = pconn->get_send_stream().get_read_seg_len();

            this->post_placement_write(pconn, (char*)seg_ptr, seg_len);
        }
    }
}

void inetwork_imp::on_accept(net_conn* listen_conn, net_conn* accept_conn)
{
    // �����׽��ָ��������ģ��Ա����ͨ��getpeername��ȡ��ip��ַ��
    setsockopt(accept_conn->get_socket(), 
        SOL_SOCKET, 
        SO_UPDATE_ACCEPT_CONTEXT,  
        (char*)&( listen_conn->get_socket() ), 
        sizeof( listen_conn->get_socket()) );

    // ��ȡ�Զ�ip����Ϣ
    accept_conn->init_peer_info();

    // �ٴ�Ͷ���µĽ�����������
    this->post_accept(listen_conn);

    // �����ϲ㴦���¼�,��INetClientImp
    net_event_handler_->on_accept(listen_conn, accept_conn); 

    // ����ʱ���
    listen_conn->upsate_last_active_tm();
    accept_conn->upsate_last_active_tm();

    //Ͷ�ݶ���������Ͽ������ڲ��ᴦ��
    this->post_read(accept_conn);
}

void inetwork_imp::on_connect(net_conn* pConn, bool bsuccess)
{
    // ���ӳɹ���֪ͨ�ϲ㴦���¼�
    net_event_handler_->on_connect(pConn, true);

    // �����ϴλ�Ծ��ʱ���
    pConn->upsate_last_active_tm();

    // Ͷ�ݶ�����
    this->post_read(pConn);
}

net_conn* inetwork_imp::create_listen_conn(USHORT uLocalPort)
{
    net_conn* pListenConn = create_conn();
    if (NULL == pListenConn) {
        return NULL;
    }

    SOCKET& sockListen = pListenConn->get_socket();

    pListenConn->set_local_port(uLocalPort);
    pListenConn->dis_connect();
    sockListen = ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);
    if (INVALID_SOCKET == sockListen) {
        release_conn(pListenConn);
        return NULL;
    }

    sockaddr_in local_addr;
    ZeroMemory(&local_addr, sizeof(sockaddr_in));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(pListenConn->get_local_port());
    int irt = ::bind(sockListen, (sockaddr*)(&local_addr), sizeof(sockaddr_in));
    listen(sockListen, 5);

    if (SOCKET_ERROR == irt) 
    {
        release_conn(pListenConn);
        return NULL;
    }

    HANDLE hResult = CreateIoCompletionPort((HANDLE)sockListen,hiocp_, (ULONG_PTR)pListenConn, 0);
    if (NULL == hResult) 
    {
        _ASSERT(FALSE);
        release_conn(pListenConn);
        return NULL;
    }

    return pListenConn;
}

net_conn* inetwork_imp::create_conn() 
{
    net_conn* pConn = NULL;
    guard guard_(free_net_conn_mutex_);
    if (!free_net_conn_list_.empty()) 
    {
        pConn = free_net_conn_list_.front();
        new (pConn) net_conn;                   //placement new
        free_net_conn_list_.pop_front();
    }
    else 
    {
        pConn = new net_conn;
    }
    return pConn;
}

bool inetwork_imp::release_conn(net_conn* pConn)
{
    _ASSERT(pConn);
    auto_lock helper(free_net_conn_mutex_);

#ifdef _DEBUG

    INetConnListType::const_iterator itr;
    itr = free_net_conn_list_.begin();
    for (; itr != free_net_conn_list_.end(); ++itr) 
    {
        if ((*itr) == pConn) 
        {
            _ASSERT(FALSE && "��������⣬�������!");
        }
    }
#endif

    if (free_net_conn_list_.size() > 1000) 
    {
        delete pConn;
    }
    else 
    {
        pConn->~net_conn();
        free_net_conn_list_.push_back(pConn);
    }

    return true;
}

void inetwork_imp::worker_thread_(void* param)
{
    //ʹ����ɶ˿�ģ��
    net_overLapped *lpOverlapped = NULL;
    DWORD		    dwByteTransfered = 0;
    net_conn*       pConn = NULL;

    while (true)
    {
        lpOverlapped = NULL;

        // ����ĺ������þ���ȥI/O��������ȴ��������I/O�������
        BOOL bResult = GetQueuedCompletionStatus(
            this->hiocp_, // ָ�����ĸ�IOCP����������
            &dwByteTransfered, // ��û��Ƿ����˶����ֽڵ�����
            (PULONG_PTR)&pConn, // socket������IOCPʱָ����һ������ֵ
            (LPWSAOVERLAPPED*)&lpOverlapped,  // ���ConnectEx �������Ľṹ
            INFINITE);				// һֱ�ȴ���ֱ���н��

        _ASSERT(lpOverlapped);
        if (lpOverlapped == NULL)  
        {
            //TRACE(TEXT("�˳�...."));
            return ;
        }

        if (bResult) 
        {
            if(dwByteTransfered == -1 && lpOverlapped == NULL) 
            {
                //TRACE(TEXT("�˳��̲߳�����..."));
                return ;
            }

            switch(lpOverlapped->operate_type_ )
            {
            case OP_READ:
                {
                    this->on_read(pConn, (const char*) lpOverlapped->recv_buff_, dwByteTransfered);
                }
                break;

            case OP_WRITE:
                {
                    this->on_write(pConn, dwByteTransfered);
                }
                break;
            case OP_ACCEPT:
                {
                    net_conn* pAcceptConn = (net_conn*) lpOverlapped->pend_data_;
                    this->on_accept(pConn, pAcceptConn);
                }
                break;

            case OP_CONNECT:
                {
                    this->on_connect(pConn, true);
                }
                break;
            }

            //�ͷ�lpOverlapped�ṹ��ÿ���������ʱ�����»�ȡ
            this->release_net_overlapped(lpOverlapped);
        }
        else 
        {
            //@todo ��Ҫ�����Ͽ����ӵĴ���

            // ����Ͷ�ݼ���
            if (lpOverlapped->operate_type_ == OP_READ) 
            {
                guard guard_(pConn->get_lock());
                pConn->dec_post_read_count();
            }
            else if (lpOverlapped->operate_type_ == OP_WRITE) 
            {
                guard guard_(pConn->get_lock());
                pConn->dec_post_write_count();
            }

            if (lpOverlapped->operate_type_ == OP_CONNECT) 
            {
                // ��������ʧ�ܵ�ʱ���ȹر����ӣ�֪ͨ�ϲ㴦����Ȼ���ͷ����Ӷ���
                pConn->dis_connect();
                net_event_handler_->on_connect(pConn, false);

                this->release_conn(pConn);
            }
            else if (lpOverlapped->operate_type_ == OP_ACCEPT) 
            {
                // ��������ʧ��,�ͷ�����
                net_conn* pListConn = pConn;                                    // �����׽���
                net_conn* pAcceptConn = (net_conn*) lpOverlapped->pend_data_;   // ���յ�����
                this->post_accept(pListConn);                              // ����Ͷ�ݽ�����������
                net_event_handler_->on_accept(pListConn, pAcceptConn, false);    // ֪ͨ�ϲ��������ʧ��

                this->release_conn(pAcceptConn);                           // �ͷ�
            }
            else 
            {
                // ������������������Ҫ�ͷ����ӣ�
                pConn->dis_connect();
                this->check_and_disconnect(pConn);
                this->release_conn(pConn);
            }

            this->release_net_overlapped(lpOverlapped);
        } //if (bResult) {
    }

    return;
}

void inetwork_imp::init_fixed_overlapped_list(size_t nCount)
{
    net_overLapped* pList = new net_overLapped[nCount];
    if (NULL == pList) {
        return;
    }

    for (size_t i=0; i<nCount; ++i)
    {
        pList[i].bFixed = true;
        overlapped_list_.push_back(&pList[i]);
    }
}

net_overLapped* inetwork_imp::get_net_overlapped()
{
    auto_lock helper(overlapped_list_mutex_);

    net_overLapped* ptmp = NULL;
    if (!overlapped_list_.empty()) {
        ptmp = overlapped_list_.front();
        overlapped_list_.pop_front();

        bool bFix = ptmp->bFixed;
        memset(ptmp, 0, sizeof(OVERLAPPED));
        ptmp->bFixed = bFix;

        //TRACE(TEXT("���ڴ���л�ȡnet_overLapped..."));
    }
    else {
        //TRACE(TEXT("�½�net_overLapped�ṹ..."));
        ptmp = new net_overLapped;
    }

    return ptmp;
}

bool inetwork_imp::release_net_overlapped(net_overLapped* pMyoverlapped)
{
    _ASSERT(pMyoverlapped);
    if (NULL == pMyoverlapped) {
        return false;
    }

    auto_lock helper(overlapped_list_mutex_);

    if (!pMyoverlapped->bFixed) {
        delete pMyoverlapped;
        //TRACE(TEXT("û���棬�ͷ��ڴ�..."));
    }
    else {
        //TRACE(TEXT("����Overlapped��������..."));
        overlapped_list_.push_back(pMyoverlapped);
    }

    return true;
}

void inetwork_imp::check_and_disconnect(net_conn* pConn)
{
    pConn->lock();
    if (!pConn->get_is_closing() && pConn->get_post_read_count() == 0 && pConn->get_post_read_count() == 0) 
    {
        pConn->set_is_closing(TRUE);
        pConn->unlock();

        net_event_handler_->on_disconnect(pConn);
    }
    else 
    {
        pConn->unlock();
    }
}