/* SweepOptimizer */

#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <list>
#include <string>
#include <future>
#include <mutex>
#include <tuple>
#include <deque>
#include <vector>

using std::cout;
using std::endl;
using std::string;
using std::vector;

enum Floor : size_t{
	Dirty        = 1,	//拭かれていない床
	Clean        = 2,	//拭いた床
	Boy          = 4,	//男の子
	Girl         = 8,	//女の子
	Robot       = 16,	//ロボット
	Pool        = 32,	//水たまり(男の子しか処理できない)
	Apple       = 64,	//リンゴ(女の子しか処理できない)
	Bottle     = 128,	//ビン(ロボットしか処理できない)
	DustBox    = 256,	//ゴミ箱(リンゴの捨て場所)
	RecycleBox = 512,	//リサイクル箱(ビンの捨て場所)
	Obstacle  = 1024,	//障害物
	Types = 11,			//種類数
	// 移動可能な場所
	CanMoveFlg = Dirty | Clean | Boy | Girl | Robot | Pool | Apple | Bottle,
	// 掃除しなければならない場所
	MustCleanFlg = Dirty | Pool | Apple | Bottle,
};

struct Status {
	Floor type_;			//掃除人の種類
	size_t move_now_;		//現在の歩数
	size_t move_max_;		//最大歩数
	size_t position_now_;	//現在の位置
	size_t position_old_;	//過去の位置
	size_t position_first_;	//最初の位置
	size_t stock_;			//リンゴ・ビンの所持数
	size_t move_max_combo_;		//最大歩数(コンボ用)
};

const size_t kCleanerTypes = 3;	//掃除人の種類数(男の子・女の子・ロボット)
const std::array<Floor, Floor::Types> floor_types{ Floor::Dirty,Floor::Clean,Floor::Boy,Floor::Girl,Floor::Robot,Floor::Pool,Floor::Apple,Floor::Bottle,Floor::DustBox,Floor::RecycleBox,Floor::Obstacle };

inline bool CanMoveFloor(const Floor floor) noexcept {
	return (floor & Floor::CanMoveFlg) != 0;
}

inline bool MustCleanFloor(const Floor floor) noexcept {
	return (floor & Floor::MustCleanFlg) != 0;
}

// 並列処理用
size_t g_threads = 1;
std::mutex g_mutex;
bool g_solved_flg = false;

class Query{
	// 盤面サイズ
	size_t x_, y_;
	size_t x_mini_, y_mini_;
	// 床の状態
	vector<Floor> floor_;
	// 掃除人の種類・現在の歩数・最大歩数・現在の位置・過去の位置
	vector<Status> cleaner_status_;
	// 最大歩数の最大
	size_t max_depth_;
	// 解答における、各掃除人の移動経路
	vector<std::list<size_t>> cleaner_move_;
	// マスA→マスBへの最小移動歩数
	vector<vector<size_t>> min_cost_;
	vector<vector<size_t>> min_cost_combo_;	//コンボ用なので上限が緩い
	// 周囲にゴミ箱/リサイクル箱があったらtrue
	vector<char> near_dustbox_, near_recyclebox_;
	// 実行時のスレッド数
	size_t max_threads_;
public:
	// コンストラクタ
	Query(const char file_name[], const size_t max_threads){
		max_threads_ = max_threads;
		std::ifstream fin;
		fin.exceptions(std::ifstream::failbit | std::ifstream::badbit);
		fin.open(file_name);
		// 盤面サイズを読み込む
		size_t x, y;
		fin >> x >> y;
		x_mini_ = x; y_mini_ = y;
		x_ = x + 2; y_ = y + 2;	//番兵用に拡張する
		floor_.resize(x_ * y_, Floor::Obstacle);
		// 盤面データを読み込み、反映させる
		vector<vector<Status>> cleaner_status_temp;
		cleaner_status_temp.resize(kCleanerTypes);
		for (size_t j = 1; j <= y; ++j) {
			for (size_t i = 1; i <= x; ++i) {
				size_t temp;
				fin >> temp;
				if (temp >= Floor::Types) temp = Floor::Obstacle;
				size_t position = j * x_ + i;
				switch (floor_[position] = floor_types[temp]) {
				case Floor::Boy:
					cleaner_status_temp[0].push_back(Status{ Floor::Boy, 0, 0, position, position, position, 0});
					floor_[position] = Floor::Clean;
					break;
				case Floor::Girl:
					cleaner_status_temp[1].push_back(Status{ Floor::Girl, 0, 0, position, position, position, 0 });
					floor_[position] = Floor::Clean;
					break;
				case Floor::Robot:
					cleaner_status_temp[2].push_back(Status{ Floor::Robot, 0, 0, position, position, position, 0 });
					floor_[position] = Floor::Clean;
					break;
				default:
					break;
				}
			}
		}
		// 掃除人データを読み込み、反映させる
		max_depth_ = 0;
		for(size_t ti = 0; ti < kCleanerTypes; ++ti){
			size_t temp;
			fin >> temp;
			if(cleaner_status_temp[ti].size() != temp){
				cout << "問題データに誤りがあります." << endl;
				throw;
			}
			for(size_t ci = 0; ci < cleaner_status_temp[ti].size(); ++ci){
				fin >> temp;
				cleaner_status_temp[ti][ci].move_max_ = temp;
				cleaner_status_temp[ti][ci].move_max_combo_ = temp + 2;
				max_depth_ = std::max(max_depth_, temp);
			}
		}
		for (auto &it_t : cleaner_status_temp) {
			for (auto &it_c : it_t) {
				cleaner_status_.push_back(it_c);
			}
		}
		cleaner_move_.resize(cleaner_status_.size());
		// 事前に最小移動歩数を計算しておく(ワーシャル・フロイド法)
		min_cost_.resize(x_ * y_);
		const size_t kMaxMoveCost = x_ * y_ + 1;
		for (size_t k = 0; k < x_ * y_; ++k) {
			min_cost_[k].resize(x_ * y_, kMaxMoveCost);
		}
		for (size_t iy = 1; iy <= y; ++iy) {
			for (size_t ix = 1; ix <= x; ++ix) {
				size_t i = iy * x_ + ix;
				if (!CanMoveFloor(floor_[i])) continue;
				for (size_t jy = 1; jy <= y; ++jy) {
					for (size_t jx = 1; jx <= x; ++jx) {
						size_t j = jy * x_ + jx;
						if (!CanMoveFloor(floor_[j])) continue;
						if (i == j) {
							 min_cost_[i][j] = 0;
							continue;
						}
						if (i + 1 == j || j + 1 == i || i + x_ == j || j + x_ == i) {
							min_cost_[i][j] = 1;
							continue;
						}
					}
				}
			}
		}
		for (size_t i = 0; i < x_ * y_; ++i) {
			for (size_t j = 0; j < x_ * y_; ++j) {
				for (size_t k = 0; k < x_ * y_; ++k) {
					min_cost_[j][k] = std::min(min_cost_[j][k], min_cost_[j][i] + min_cost_[i][k]);
				}
			}
		}
		// 事前に周囲にゴミ箱/リサイクル箱があるかを判定しておく
		near_dustbox_.resize(x_ * y_, 0);
		near_recyclebox_.resize(x_ * y_, 0);
		for (size_t j = 1; j <= y_mini_; ++j) {
			for (size_t i = 1; i <= x_mini_; ++i) {
				size_t position = j * x_ + i;
				if (floor_[position - x_] == Floor::DustBox
					|| floor_[position - 1] == Floor::DustBox
					|| floor_[position + 1] == Floor::DustBox
					|| floor_[position + x_] == Floor::DustBox) {
					near_dustbox_[position] = 1;
				}
				if (floor_[position - x_] == Floor::RecycleBox
					|| floor_[position - 1] == Floor::RecycleBox
					|| floor_[position + 1] == Floor::RecycleBox
					|| floor_[position + x_] == Floor::RecycleBox) {
					near_recyclebox_[position] = 1;
				}
			}
		}
	}
	// ヘルパー関数
	string GetPos(const size_t position) const{
		return "[" + std::to_string(position % x_ - 1) + "," + std::to_string(position / x_ - 1) + "]";
	}
	// 盤面表示
	void Put() const noexcept{
		cout << "横" << x_mini_ << "マス,縦" << y_mini_ << "マス" << endl;
		for(size_t j = 1; j <= y_mini_; ++j){
			for(size_t i = 1; i <= x_mini_; ++i){
				switch (floor_[j * x_ + i]) {
				case Floor::Dirty:
					cout << "□";
					break;
				case Floor::Clean:
					cout << "×";
					break;
				case Floor::Boy:
					cout << "♂";
					break;
				case Floor::Girl:
					cout << "♀";
					break;
				case Floor::Robot:
					cout << "Ｒ";
					break;
				case Floor::Pool:
					cout << "水";
					break;
				case Floor::Apple:
					cout << "実";
					break;
				case Floor::Bottle:
					cout << "瓶";
					break;
				case Floor::DustBox:
					cout << "ゴ";
					break;
				case Floor::RecycleBox:
					cout << "リ";
					break;
				case Floor::Obstacle:
					cout << "■";
					break;
				}
			}
			cout << endl;
		}
		for (auto &it_c : cleaner_status_) {
			auto type = it_c.type_;
			auto position = it_c.position_now_;
			auto move_now = it_c.move_now_;
			auto move_max = it_c.move_max_;
			switch (type) {
			case Floor::Boy:
				cout << "男の子";
				break;
			case Floor::Girl:
				cout << "女の子";
				break;
			case Floor::Robot:
				cout << "Robot";
				break;
			}
			cout << GetPos(position) << "(" << move_now << "/" << move_max << ")歩 ";
		}
		cout << endl;
	}
	// 終了判定
	bool Sweeped() {
		for (size_t j = 1; j <= y_mini_; ++j) {
			for (size_t i = 1; i <= x_mini_; ++i) {
				size_t k = j * x_ + i;
				if (MustCleanFloor(floor_[k])) return false;
			}
		}
		for (auto &it_c : cleaner_status_) {
			if (it_c.stock_ != 0) return false;
		}
		return true;
	}
	// 現状では拭ききれない場合はfalse(nは許容量)
	bool CanMoveWithCombo() const noexcept {
		for (size_t j = 1; j <= y_mini_; ++j) {
			for (size_t i = 1; i <= x_mini_; ++i) {
				const size_t position = j * x_ + i;
				// 拭かなくてもいいマスは無視する
				auto &cell = floor_[position];
				if (!MustCleanFloor(cell)) continue;
				// 拭く必要がある場合は調査する
				bool can_move_flg = false;
				for (const auto &it_c : cleaner_status_) {
					if (min_cost_[position][it_c.position_now_] + it_c.move_now_ <= it_c.move_max_combo_) {
						if ((cell == Floor::Pool && it_c.type_ != Floor::Boy)
							|| (cell == Floor::Apple && it_c.type_ != Floor::Girl)
							|| (cell == Floor::Bottle && it_c.type_ != Floor::Robot)) continue;
						can_move_flg = true;
						break;
					}
				}
				if (!can_move_flg) {
					return false;
				}
			}
		}
		return true;
	}
	bool CanMoveNonCombo() const noexcept {
		for (size_t j = 1; j <= y_mini_; ++j) {
			for (size_t i = 1; i <= x_mini_; ++i) {
				const size_t position = j * x_ + i;
				// 拭かなくてもいいマスは無視する
				auto &cell = floor_[position];
				if (!MustCleanFloor(cell)) continue;
				// 拭く必要がある場合は調査する
				bool can_move_flg = false;
				for (const auto &it_c : cleaner_status_) {
					if (min_cost_[position][it_c.position_now_] + it_c.move_now_ <= it_c.move_max_) {
						if ((cell == Floor::Pool && it_c.type_ != Floor::Boy)
							|| (cell == Floor::Apple && it_c.type_ != Floor::Girl)
							|| (cell == Floor::Bottle && it_c.type_ != Floor::Robot)) continue;
						can_move_flg = true;
						break;
					}
				}
				if (!can_move_flg) {
					return false;
				}
			}
		}
		return true;
	}
	// 範囲攻撃
	void CleanCombo() noexcept {
		for (size_t ci1 = 0; ci1 < cleaner_status_.size() - 1; ++ci1) {
			size_t position = cleaner_status_[ci1].position_now_;
			for (size_t ci2 = ci1 + 1; ci2 < cleaner_status_.size(); ++ci2) {
				if (position == cleaner_status_[ci2].position_now_ && cleaner_status_[ci1].move_now_ == cleaner_status_[ci2].move_now_) {
					// 範囲攻撃発動！
					for (int i = -1; i <= 1; ++i) {
						for (int j = -1; j <= 1; ++j) {
							if (floor_[position + i + j * x_] == Floor::Dirty) floor_[position + i + j * x_] = Floor::Clean;
						}
					}
				}
			}
		}
	}
	// 周囲にゴミ箱/リサイクル箱があった際に捨てる
	size_t SurroundedBox(const Status &cleaner) const noexcept {
		switch (cleaner.type_) {
		case Floor::Girl:
			if (near_dustbox_[cleaner.position_now_] != 0) return 0;
			break;
		case Floor::Robot:
			if (near_recyclebox_[cleaner.position_now_] != 0) return 0;
			break;
		default:
			break;
		}
		return cleaner.stock_;
	}
	// 汚れやゴミなどがあった場合は掃除する
	void CleanFloor(Floor &floor, Status &cleaner) noexcept{
		switch (floor) {
		case Floor::Dirty:
			floor = Floor::Clean;
			break;
		case Floor::Clean:
			break;
		case Floor::Pool:
			if (cleaner.type_ == Floor::Boy) floor = Floor::Clean;
			break;
		case Floor::Apple:
			if (cleaner.type_ == Floor::Girl) {
				++cleaner.stock_;
				floor = Floor::Clean;
			}
			break;
		case Floor::Bottle:
			if (cleaner.type_ == Floor::Robot) {
				++cleaner.stock_;
				floor = Floor::Clean;
			}
			break;
		default:
			break;
		}
	}
	// 指定地点へ移動させる
	void MoveCleanerForward(const size_t ci, const size_t next_position) noexcept{
		auto &it_c = cleaner_status_[ci];
		auto &floor_ref = floor_[next_position];
		it_c.position_old_ = it_c.position_now_;
		it_c.position_now_ = next_position;
		++it_c.move_now_;
		it_c.stock_ = SurroundedBox(it_c);
		CleanFloor(floor_ref, it_c);
	}
	// 手を戻す
	void MoveCleanerBack(const size_t ci, const size_t next_position) noexcept{
		auto &it_c = cleaner_status_[ci];
		it_c.position_now_ = it_c.position_old_;
		--it_c.move_now_;
	}
	// 探索ルーチン
	bool MoveWithCombo(const size_t depth, const size_t index) {
		if (g_solved_flg) return false;
		// 全員を1歩だけ進める＝depthと等しい歩数の掃除人がいない
		for (size_t ci = index; ci < cleaner_status_.size(); ++ci) {
			auto &it_c = cleaner_status_[ci];
			// 歩を進めるべきではない掃除人は飛ばす
			if (it_c.move_now_ != depth) continue;
			if (it_c.move_now_ == it_c.move_max_) continue;
			const auto position = it_c.position_now_;
			// 上下左右の動きについて議論する
			if (g_threads < max_threads_) {
				vector<size_t> next_position;
				for (const auto next_position_ : { position - x_, position - 1, position + 1, position + x_ }) {
					// すぐ前に行った場所にバックするのは禁じられている
					if (next_position_ == it_c.position_old_) continue;
					// 障害物は乗り越えられない
					auto &floor_ref = floor_[next_position_];
					if (!CanMoveFloor(floor_ref)) continue;
					next_position.push_back(next_position_);
				}
				vector<std::future<bool>> result(next_position.size());
				std::deque<bool> result_get(next_position.size());
				vector<Query> query_back(next_position.size(), *this);
				g_mutex.lock(); g_threads += next_position.size() - 1; g_mutex.unlock();
				for (size_t di = 0; di < next_position.size(); ++di) {
					result[di] = std::async(std::launch::async, [this, &it_c, next_position, ci, di, &query_back, depth] {
						const auto old_position = it_c.position_old_;
						const auto old_stock = it_c.stock_;
						const auto old_floor = floor_[next_position[di]];
						query_back[di].MoveCleanerForward(ci, next_position[di]);
						// 移動処理
						if (!query_back[di].MoveWithCombo(depth, ci + 1)) return false;
						query_back[di].cleaner_move_[ci].push_front(next_position[di]);
						return true;
					});
				}
				for (size_t di = 0; di < next_position.size(); ++di) {
					result_get[di] = result[di].get();
				}
				g_mutex.lock(); g_threads -= next_position.size() - 1; g_mutex.unlock();
				for (size_t di = 0; di < next_position.size(); ++di) {
					if (result_get[di]) {
						*this = std::move(query_back[di]);
						return true;
					}
				}
				return false;
			}
			else {
				for (const auto next_position : { position - x_, position - 1, position + 1, position + x_ }) {
					// すぐ前に行った場所にバックするのは禁じられている
					if (next_position == it_c.position_old_) continue;
					// 障害物は乗り越えられない
					auto &floor_ref = floor_[next_position];
					if (!CanMoveFloor(floor_ref)) continue;
					// 移動を行う
					const auto old_position = it_c.position_old_;
					const auto old_floor = floor_ref;
					const auto old_stock = it_c.stock_;
					MoveCleanerForward(ci, next_position);
					// 移動処理
					if (MoveWithCombo(depth, ci + 1)) {
						cleaner_move_[ci].push_front(next_position);
						return true;
					}
					// 元に戻す
					MoveCleanerBack(ci, next_position);
					it_c.position_old_ = old_position;
					floor_ref = old_floor;
					it_c.stock_ = old_stock;
				}
			}
			return false;
		}
		// 再帰深さが最大の時は、解けているかどうかをチェックする
		if (depth >= max_depth_) {
			// 盤面が埋まっているかをチェックする
			if (Sweeped()) {
				g_mutex.lock();
				//Put();
				g_solved_flg = true;
				g_mutex.unlock();
				return true;
			}
			else {
				return false;
			}
		}
		// min_cost_による枝刈りを行う
		if (!CanMoveWithCombo()) return false;
		// 同タイミングで複数人がコラボすることによる範囲攻撃を考慮する
		vector<Floor> floor_back = floor_;
		CleanCombo();
		bool flg = MoveWithCombo(depth + 1, 0);
		floor_ = floor_back;
		return flg;
	}
	bool MoveNonCombo(const size_t depth, const size_t index){
		if (g_solved_flg) return false;
		// 全員を1歩だけ進める＝depthと等しい歩数の掃除人がいない
		for(size_t ci = index; ci < cleaner_status_.size(); ++ci){
			auto &it_c = cleaner_status_[ci];
			// 歩を進めるべきではない掃除人は飛ばす
			if (it_c.move_now_ != depth) continue;
			if (it_c.move_now_ == it_c.move_max_) continue;
			const auto position = it_c.position_now_;
			// 上下左右の動きについて議論する
			vector<size_t> next_position;
			for (const auto next_position_ : { position - x_, position - 1, position + 1, position + x_ }) {
				// すぐ前に行った場所にバックするのは禁じられている
				if (next_position_ == it_c.position_old_) continue;
				// 障害物は乗り越えられない
				auto &floor_ref = floor_[next_position_];
				if (!CanMoveFloor(floor_ref)) continue;
				next_position.push_back(next_position_);
			}
			// 手を並び替えておく
			for (size_t di = 0; di < next_position.size() - 1; ++di) {
				for (size_t dj = di + 1; dj < next_position.size(); ++dj) {
					if (!MustCleanFloor(floor_[next_position[di]]) && MustCleanFloor(floor_[next_position[dj]])) {
						size_t temp = next_position[di];
						next_position[di] = next_position[dj];
						next_position[dj] = temp;
					}
				}
			}
			// スレッド数によって分岐
			if (g_threads < max_threads_) {
				vector<std::future<bool>> result(next_position.size());
				std::deque<bool> result_get(next_position.size());
				vector<Query> query_back(next_position.size(), *this);
				g_mutex.lock(); g_threads += next_position.size() - 1; g_mutex.unlock();
				for (size_t di = 0; di < next_position.size(); ++di) {
					result[di] = std::async(std::launch::async, [this, &it_c, next_position, ci, di, &query_back, depth] {
						const auto old_position = it_c.position_old_;
						const auto old_stock = it_c.stock_;
						const auto old_floor = floor_[next_position[di]];
						query_back[di].MoveCleanerForward(ci, next_position[di]);
						// 移動処理
						if (!query_back[di].MoveNonCombo(depth, ci + 1)) return false;
						query_back[di].cleaner_move_[ci].push_front(next_position[di]);
						return true;
					});
				}
				for (size_t di = 0; di < next_position.size(); ++di) {
					result_get[di] = result[di].get();
				}
				g_mutex.lock(); g_threads -= next_position.size() - 1; g_mutex.unlock();
				for (size_t di = 0; di < next_position.size(); ++di) {
					if (result_get[di]) {
						*this = std::move(query_back[di]);
						return true;
					}
				}
				return false;
			}
			else {
				for (size_t di = 0; di < next_position.size(); ++di) {
					auto &floor_ref = floor_[next_position[di]];
					// 移動を行う
					const auto old_position = it_c.position_old_;
					const auto old_floor = floor_ref;
					const auto old_stock = it_c.stock_;
					MoveCleanerForward(ci, next_position[di]);
					// 移動処理
					if (MoveNonCombo(depth, ci + 1)) {
						cleaner_move_[ci].push_front(next_position[di]);
						return true;
					}
					// 元に戻す
					MoveCleanerBack(ci, next_position[di]);
					it_c.position_old_ = old_position;
					floor_ref = old_floor;
					it_c.stock_ = old_stock;
				}
			}
			return false;
		}
		// 再帰深さが最大の時は、解けているかどうかをチェックする
		if (depth >= max_depth_) {
			// 盤面が埋まっているかをチェックする
			if (Sweeped()) {
				g_mutex.lock();
				//Put();
				g_solved_flg = true;
				g_mutex.unlock();
				return true;
			}
			else {
				return false;
			}
		}
		// min_cost_による枝刈りを行う
		if (!CanMoveNonCombo()) return false;
		return MoveNonCombo(depth + 1, 0);
	}
	// 解答を表示する
	void ShowAnswer() {
		for (size_t ci = 0; ci < cleaner_status_.size(); ++ci) {
			switch (cleaner_status_[ci].type_) {
			case Floor::Boy:
				cout << "男の子";
				break;
			case Floor::Girl:
				cout << "女の子";
				break;
			case Floor::Robot:
				cout << "Robot";
				break;
			}
			cout << " " << GetPos(cleaner_status_[ci].position_first_);
			size_t old_position = cleaner_status_[ci].position_first_;
			size_t count = 0;
			for (auto it_m : cleaner_move_[ci]) {
				cout << "->" << GetPos(it_m);
				if (old_position + 1 == it_m) {
					cout << "(右)";
				}
				else if (it_m + 1 == old_position) {
					cout << "(左)";
				}
				else if (old_position + x_ == it_m) {
					cout << "(下)";
				}
				else {
					cout << "(上)";
				}
				old_position = it_m;
				++count;
				if (count % 5 == 0) cout << "\n　　　";
			}
			cout << endl;
		}
	}
};

int main(int argc, char *argv[]){
	if(argc < 2) return -1;
	int max_threads = 1;
	if (argc >= 3) {
		max_threads = std::stoi(argv[2]);
		if (max_threads < 1) max_threads = 1;
	}
	Query query(argv[1], max_threads);
	query.Put();
	const auto process_begin_time = std::chrono::high_resolution_clock::now();
	bool flg = query.MoveNonCombo(0, 0);
	auto process_end_time = std::chrono::high_resolution_clock::now();
	if (!flg) {
		cout << "..." << std::chrono::duration_cast<std::chrono::milliseconds>(process_end_time - process_begin_time).count() << "[ms]..." << endl;
		flg = query.MoveWithCombo(0, 0);
		process_end_time = std::chrono::high_resolution_clock::now();
	}
	if (flg) query.ShowAnswer();
	cout << "処理時間：" << std::chrono::duration_cast<std::chrono::milliseconds>(process_end_time - process_begin_time).count() << "[ms]\n" << endl;
	return 0;
}
