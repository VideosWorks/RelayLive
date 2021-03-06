#include "stdafx.h"
#include "libwebsockets.h"
#include "HttpWebServer.h"
#include <string>
#include "DeviceMgr.h"
#include "LiveWorker.h"

namespace HttpWsServer
{
#define G_BYTES (1024 * 1024 * 1024) // 1GB
#define M_BYTES (1024 * 1024)		 // 1MB
#define K_BYTES 1024				 // 1KB

    static std::string mount_web_origin("./home");  //站点本地位置

    static uint32_t make_dir_info(string urlStr, string path, string &strHtml) {
        std::string strFiles(""); // 普通文件写在这个字符串中.
        char buffer[MAX_PATH + 100] = {0};
        char sizeBuf[MAX_PATH + 100] = {0};
        // 1. 输出HTML头,并指定UTF-8编码格式
        strHtml = "<html><head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\"/></head>";
        strHtml.append("<body>");
        // 2. 输出路径
        //(1). 输出第一项 根目录
        strHtml.append("<A href=\"/\">/</A>");
        //(2). 其它目录
        uv_fs_t req;
        uv_dirent_t dent;
        int sr = uv_fs_scandir(NULL, &req, path.c_str(), 0, NULL);
        if(sr < 0) {
            strHtml = "error path is not dir";
            return HTTP_STATUS_FORBIDDEN;
        }
        std::string::size_type st = 1;
        std::string::size_type stNext = 1;
        while( (stNext = urlStr.find('/', st)) != std::string::npos)
        {
            std::string strDirName =  urlStr.substr(st, stNext - st + 1);
            std::string strSubUrl = urlStr.substr(0, stNext + 1);

            //strHtml.append("&nbsp;|&nbsp;");

            strHtml.append("<A href=\"");
            strHtml.append(strSubUrl);
            strHtml.append("\">");
            strHtml.append(strDirName);
            strHtml.append("</A>");

            // 下一个目录
            st = stNext + 1;
        }
        strHtml.append("<br /><hr />");
        // 3. 列出当前目录下的所有文件
        while (uv_fs_scandir_next(&req, &dent) != UV_EOF) {
            Log::debug("find dir:%s", dent.name);
            // 跳过 . 文件
            if( _stricmp(dent.name, ".") == 0 || 0 == _stricmp(dent.name, "..") )
                continue;

            if (dent.type & UV_DIRENT_DIR)
            {
                // 如果是子目录,直接写入
                string sub_path = path;
                sub_path.append(dent.name);
                uv_fs_t stareq;
                uv_fs_stat(NULL, &stareq, sub_path.c_str(), NULL);
                uv_stat_t* stat = uv_fs_get_statbuf(&stareq);
                time_t t = (time_t)stat->st_mtim.tv_sec;
                CTimeFormat::printTime(&t, "%Y-%m-%d %H:%M:%S", buffer);
                strHtml.append(buffer);
                uv_fs_req_cleanup(&stareq);

                // 目录名需要转换为UTF8编码
                sprintf_s(buffer, MAX_PATH + 100, "%s/", dent.name);
                std::string fileurl = urlStr;
                std::string filename = buffer;

                strHtml.append("&nbsp;&nbsp;");
                strHtml.append("<A href=\"");
                strHtml.append(fileurl.c_str());
                strHtml.append(filename.c_str());
                strHtml.append("\">");
                strHtml.append(filename.c_str());
                strHtml.append("</A>");

                // 写入目录标志
                strHtml.append("&nbsp;&nbsp;[DIR]");

                // 换行
                strHtml.append("<br />");
            } else if (dent.type & UV_DIRENT_FILE) {
                // 普通文件,写入到一个缓冲的字符串string变量内,循环外再合并.这样,所有的目录都在前面,文件在后面
                string sub_path = path;
                sub_path.append(dent.name);
                uv_fs_t stareq;
                uv_fs_stat(NULL, &stareq, sub_path.c_str(), NULL);
                uv_stat_t* stat = uv_fs_get_statbuf(&stareq);
                time_t t = (time_t)stat->st_mtim.tv_sec;
                CTimeFormat::printTime(&t, "%Y-%m-%d %H:%M:%S", buffer);
                strFiles += EncodeConvert::AtoUTF8(buffer);

                // 文件名转换为UTF8编码再写入
                std::string filename = dent.name;
                std::string fileurl = urlStr;

                strFiles += "&nbsp;&nbsp;";
                strFiles += "<A href=\"";
                strFiles += fileurl;
                strFiles += filename;
                strFiles += "\">";
                strFiles += filename;
                strFiles += "</A>";

                // 文件大小
                // 注: 由于Windows下 wsprintf 不支持 %f 参数,所以只好用 sprintf 了
                double filesize = 0;
                if( stat->st_size >= G_BYTES)
                {
                    filesize = (stat->st_size * 1.0) / G_BYTES;
                    sprintf_s(sizeBuf, MAX_PATH + 100, "%.2f&nbsp;GB", filesize);
                }
                else if( stat->st_size >= M_BYTES ) // MB
                {
                    filesize = (stat->st_size * 1.0) / M_BYTES;
                    sprintf_s(sizeBuf, MAX_PATH + 100, "%.2f&nbsp;MB", filesize);
                }
                else if( stat->st_size >= K_BYTES ) //KB
                {
                    filesize = (stat->st_size * 1.0) / K_BYTES;
                    sprintf_s(sizeBuf, MAX_PATH + 100, "%.2f&nbsp;KB", filesize);
                }
                else // Bytes
                {
                    sprintf_s(sizeBuf, MAX_PATH + 100, "%lld&nbsp;Bytes", stat->st_size);
                }
                
                strFiles += "&nbsp;&nbsp;";
                strFiles += sizeBuf;

                // 换行
                strFiles += "<br />";
                uv_fs_req_cleanup(&stareq);
            }
        } //while
        uv_fs_req_cleanup(&req);

        // 把文件字符串写入到 Content 中.
        if(strFiles.size() > 0)
        {
            strHtml.append(strFiles.c_str());
        }

        // 4. 输出结束标志.
        strHtml.append("</body></html>");

        return HTTP_STATUS_OK;
    }

    int callback_other_http(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
    {
        struct pss_other *pss = (struct pss_other *)user;

        switch (reason) {
        case LWS_CALLBACK_HTTP:
            {
                uint8_t buf[LWS_PRE + 2048], 
                    *start = &buf[LWS_PRE], 
                    *p = start,
                    *end = &buf[sizeof(buf) - LWS_PRE - 1];
                lws_snprintf(pss->path, sizeof(pss->path), "%s", (const char *)in);
                Log::debug("new request: %s", pss->path);
                unsigned int code; //返回码
                pss->html = new string();

                //对应的本地路径
                string path;
                static const string home_value = Settings::getValue("HttpServer","RootPath");
                if( !home_value.empty() ) path = home_value;
                else path = mount_web_origin;
                path.append(pss->path);

                static bool bDirVisible = Settings::getValue("HttpServer","DirVisible")=="yes"?true:false;

                //是否存在目录
                code = make_dir_info(pss->path, path, *pss->html);

                if (lws_add_http_common_headers(wsi, code,
                    "text/html",
                    pss->html->size(),
                    &p, end))
                    return 1;
                if (lws_finalize_write_http_header(wsi, start, &p, end))
                    return 1;

                lws_callback_on_writable(wsi);

                return 0;
            }
        case LWS_CALLBACK_HTTP_WRITEABLE:
            {
                if (!pss)
                    break;
                int len = pss->html->size();
                int wlen = lws_write(wsi, (uint8_t *)pss->html->c_str(), len, LWS_WRITE_HTTP_FINAL);
                if (wlen != len)
                    return 1;

                if (lws_http_transaction_completed(wsi))
                    return -1;

                return 0;
            }
        case LWS_CALLBACK_CLOSED_HTTP:
            {
                if (!pss)
                    break;
                SAFE_DELETE(pss->html);
            }

        default:
            break;
        }

        return lws_callback_http_dummy(wsi, reason, user, in, len);
    }


    static string GetDevInfo()
    {
        vector<DevInfo*> vecDev = DeviceMgr::GetDeviceInfo();
        if (vecDev.size() == 0)
        {
            return g_strError_no_device;
        }

        string strResJson = "{\"root\":[";
        for (auto dev:vecDev)
        {
            strResJson += "{";
#if 1
            if (!dev->strDevID.empty())
            {
                strResJson += "\"DeviceID\":\"";
                strResJson += dev->strDevID;
                strResJson += "\",";
            }
            if (!dev->strName.empty())
            {
                strResJson += "\"Name\":\"";
                strResJson += dev->strName;
                strResJson += "\",";
            }
            if (!dev->strManuf.empty())
            {
                strResJson += "\"Manufacturer\":\"";
                strResJson += dev->strManuf;
                strResJson += "\",";
            }
            if (!dev->strModel.empty())
            {
                strResJson += "\"Model\":\"";
                strResJson += dev->strModel;
                strResJson += "\",";
            }
            if (!dev->strOwner.empty())
            {
                strResJson += "\"Owner\":\"";
                strResJson += dev->strOwner;
                strResJson += "\",";
            }
            if (!dev->strCivilCode.empty())
            {
                strResJson += "\"CivilCode\":\"";
                strResJson += dev->strCivilCode;
                strResJson += "\",";
            }
            if (!dev->strBlock.empty())
            {
                strResJson += "\"Block\":\"";
                strResJson += dev->strBlock;
                strResJson += "\",";
            }
            if (!dev->strAddress.empty())
            {
                strResJson += "\"Address\":\"";
                strResJson += dev->strAddress;
                strResJson += "\",";
            }
            if (!dev->strParental.empty())
            {
                strResJson += "\"Parental\":\"";
                strResJson += dev->strParental;
                strResJson += "\",";
            }
            if (!dev->strParentID.empty())
            {
                strResJson += "\"ParentID\":\"";
                strResJson += dev->strParentID;
                strResJson += "\",";
            }
            if (!dev->strSafetyWay.empty())
            {
                strResJson += "\"SafetyWay\":\"";
                strResJson += dev->strSafetyWay;
                strResJson += "\",";
            }
            if (!dev->strRegisterWay.empty())
            {
                strResJson += "\"RegisterWay\":\"";
                strResJson += dev->strRegisterWay;
                strResJson += "\",";
            }
            if (!dev->strCertNum.empty())
            {
                strResJson += "\"CertNum\":\"";
                strResJson += dev->strCertNum;
                strResJson += "\",";
            }
            if (!dev->strCertifiable.empty())
            {
                strResJson += "\"Certifiable\":\"";
                strResJson += dev->strCertifiable;
                strResJson += "\",";
            }
            if (!dev->strErrCode.empty())
            {
                strResJson += "\"ErrCode\":\"";
                strResJson += dev->strErrCode;
                strResJson += "\",";
            }
            if (!dev->strEndTime.empty())
            {
                strResJson += "\"EndTime\":\"";
                strResJson += dev->strEndTime;
                strResJson += "\",";
            }
            if (!dev->strSecrecy.empty())
            {
                strResJson += "\"Secrecy\":\"";
                strResJson += dev->strSecrecy;
                strResJson += "\",";
            }
            if (!dev->strStatus.empty())
            {
                strResJson += "\"Status\":\"";
                strResJson += dev->strStatus;
                strResJson += "\",";
            }
            if (!dev->strIPAddress.empty())
            {
                strResJson += "\"IPAddress\":\"";
                strResJson += dev->strIPAddress;
                strResJson += "\",";
            }
            if (!dev->strPort.empty())
            {
                strResJson += "\"Port\":\"";
                strResJson += dev->strPort;
                strResJson += "\",";
            }
            if (!dev->strPassword.empty())
            {
                strResJson += "\"Password\":\"";
                strResJson += dev->strPassword;
                strResJson += "\",";
            }
            if (!dev->strLongitude.empty())
            {
                strResJson += "\"Longitude\":\"";
                strResJson += dev->strLongitude;
                strResJson += "\",";
            }
            if (!dev->strLatitude.empty())
            {
                strResJson += "\"Latitude\":\"";
                strResJson += dev->strLatitude;
                strResJson += "\",";
            }
            if (!dev->strPTZType.empty())
            {
                strResJson += "\"PTZType\":\"";
                strResJson += dev->strPTZType;
                strResJson += "\",";
            }
            if (!dev->strPositionType.empty())
            {
                strResJson += "\"PositionType\":\"";
                strResJson += dev->strPositionType;
                strResJson += "\",";
            }
            if (!dev->strRoomType.empty())
            {
                strResJson += "\"RoomType\":\"";
                strResJson += dev->strRoomType;
                strResJson += "\",";
            }
            if (!dev->strUseType.empty())
            {
                strResJson += "\"UseType\":\"";
                strResJson += dev->strUseType;
                strResJson += "\",";
            }
            if (!dev->strSupplyLightType.empty())
            {
                strResJson += "\"SupplyLightType\":\"";
                strResJson += dev->strSupplyLightType;
                strResJson += "\",";
            }
            if (!dev->strDirectionType.empty())
            {
                strResJson += "\"DirectionType\":\"";
                strResJson += dev->strDirectionType;
                strResJson += "\",";
            }
            if (!dev->strResolution.empty())
            {
                strResJson += "\"Resolution\":\"";
                strResJson += dev->strResolution;
                strResJson += "\",";
            }
            if (!dev->strBusinessGroupID.empty())
            {
                strResJson += "\"BusinessGroupID\":\"";
                strResJson += dev->strBusinessGroupID;
                strResJson += "\",";
            }
            if (!dev->strDownloadSpeed.empty())
            {
                strResJson += "\"DownloadSpeed\":\"";
                strResJson += dev->strDownloadSpeed;
                strResJson += "\",";
            }
            if (!dev->strSVCSpaceSupportType.empty())
            {
                strResJson += "\"SVCSpaceSupportMode\":\"";
                strResJson += dev->strSVCSpaceSupportType;
                strResJson += "\",";
            }
            if (!dev->strSVCTimeSupportType.empty())
            {
                strResJson += "\"SVCTimeSupportMode\":\"";
                strResJson += dev->strSVCTimeSupportType;
                strResJson += "\",";
            }
#endif
            //根据设备ID，查看是否在播放FLV
            CLiveWorker* pWorker = GetLiveWorker(dev->strDevID);
            if(pWorker != nullptr)
            {
                strResJson += "\"FlvClient\":";
                strResJson += pWorker->GetClientInfo();
                strResJson += ",";
            }

            strResJson = StringHandle::StringTrimRight(strResJson,',');
            strResJson += "},";
        }
        strResJson = StringHandle::StringTrimRight(strResJson,',');
        strResJson += "]}";

        return strResJson;
    }

    int callback_device_http(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
    {
        struct pss_device *pss = (struct pss_device *)user;

        switch (reason) {
        case LWS_CALLBACK_HTTP:
            {
                uint8_t buf[LWS_PRE + 2048], 
                    *start = &buf[LWS_PRE], 
                    *p = start,
                    *end = &buf[sizeof(buf) - LWS_PRE - 1];

                lws_snprintf(pss->path, sizeof(pss->path), "%s", (const char *)in);
                Log::debug("new request: %s", pss->path);

                pss->json = new string;
                *pss->json = GetDevInfo();

                if (lws_add_http_common_headers(wsi, HTTP_STATUS_OK,
                    "text/html",
                    pss->json->size(),
                    &p, end))
                    return 1;
                if (lws_finalize_write_http_header(wsi, start, &p, end))
                    return 1;

                lws_callback_on_writable(wsi);

                return 0;
            }
        case LWS_CALLBACK_HTTP_WRITEABLE:
            {
                if (!pss)
                    break;

                int len = pss->json->size();
                int wlen = lws_write(wsi, (uint8_t *)pss->json->c_str(), len, LWS_WRITE_HTTP_FINAL);
                SAFE_DELETE(pss->json)
                    if (wlen != len)
                        return 1;

                if (lws_http_transaction_completed(wsi))
                    return -1;


                return 0;
            }
        default:
            break;
        }
        //return 1;
        return lws_callback_http_dummy(wsi, reason, user, in, len);
    }
};
