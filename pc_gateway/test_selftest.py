import unittest
from selftest_view import format_selftest


class SelfTestViewTests(unittest.TestCase):
    def test_chinese_filename_and_failure(self):
        """验证GBK文件名和读取失败在日志中可辨识。"""
        report = {"run": 3, "total": 1, "received": 1, "complete": True,
                  "records": [{"index": 0, "code": 6, "result": 1,
                               "filename_hex": "报警.wav".encode("gbk").hex(),
                               "read": 512, "size": 2048}]}
        text = format_selftest(report)
        self.assertIn("报警.wav", text)
        self.assertIn("512/2048", text)
        self.assertIn("失败", text)

    def test_incomplete_summary_is_not_success(self):
        """缺失前序记录时不能把通过汇总显示为完整验收成功。"""
        text = format_selftest({"total": 3, "received": 1, "complete": False,
                                "records": [{"index": 2, "code": 10}]})
        self.assertIn("尚未接收完整", text)
        self.assertNotIn("：自检通过", text)

    def test_no_report_and_invalid_encoding(self):
        """验证未收到报告及损坏文件名不会令界面崩溃。"""
        self.assertIn("尚未收到", format_selftest({}))
        self.assertIn("编码错误", format_selftest({"total": 1, "records": [
            {"code": 2, "filename_hex": "ZZ"}]}))


if __name__ == "__main__":
    unittest.main()
