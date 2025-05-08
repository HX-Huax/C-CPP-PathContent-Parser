import pymysql
import os

# 数据库连接配置
db_config = {
    "host": "120.55.240.40",
    "user": "root",
    "password": "scpc666scpc",
    "database": "hoj",
    "port": 33060,
    "charset": "utf8mb4",
}


# 查询并保存数据
def export_code_to_files(save_path):
    try:
        # 连接数据库
        connection = pymysql.connect(**db_config)
        cursor = connection.cursor()

        # 查询 status 为 0 的数据，并获取对应的 content_type
        query = """
            SELECT j.code, j.pid, j.submit_id, l.content_type
            FROM judge j
            JOIN language l ON j.language = l.name
            WHERE j.status = 0 AND l.content_type IN ('text/x-c++src', 'text/x-csrc')
        """
        cursor.execute(query)
        results = cursor.fetchall()

        # 遍历查询结果并保存到文件
        for code, pid, submit_id, content_type in results:
            # 根据 content_type 确定文件后缀
            file_extension = ".cpp" if content_type == "text/x-c++src" else ".c"
            filename = os.path.join(save_path, f"{pid}_{submit_id}{file_extension}")
            with open(filename, "w", encoding="utf-8") as file:
                file.write(code)  # 将 code 写入文件
            print(f"Saved code to {filename}")

    except pymysql.MySQLError as e:
        print(f"Database error: {e}")
    finally:
        # 关闭数据库连接
        if connection:
            connection.close()


if __name__ == "__main__":
    save_path = "/home/hx/Dev/Project/CAM/ProProcess/examples/sql/"
    export_code_to_files(save_path)
